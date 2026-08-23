// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix fingerprint sensor button driver
 *
 * The sensor is wired to a reset GPIO and an interrupt GPIO.  Full
 * fingerprint recognition requires a proprietary SPI protocol handled
 * in userspace; this driver instead exposes the touch status of the
 * sensor as a plain input device so that the fingerprint pad can be
 * used as a hardware key (e.g. home / back).
 *
 * Copyright (c) 2026, Barnabas Czeman
 */

#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define GOODIX_FP_BUTTON_NAME "goodix_fp_button"

struct goodix_fp_button {
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *irq_gpio;
	unsigned int code;
};

static irqreturn_t goodix_fp_button_irq_handler(int irq, void *data)
{
	struct goodix_fp_button *fp = data;
	int value;

	value = gpiod_get_value(fp->irq_gpio);
	if (value < 0)
		return IRQ_HANDLED;

	input_report_key(fp->input, fp->code, value);
	input_sync(fp->input);

	return IRQ_HANDLED;
}

static int goodix_fp_button_probe(struct platform_device *pdev)
{
	struct goodix_fp_button *fp;
	struct device *dev = &pdev->dev;
	int irq;
	int err;

	fp = devm_kzalloc(dev, sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	fp->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(fp->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(fp->reset_gpio),
				     "failed to get reset GPIO\n");

	fp->irq_gpio = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(fp->irq_gpio))
		return dev_err_probe(dev, PTR_ERR(fp->irq_gpio),
				     "failed to get irq GPIO\n");

	err = device_property_read_u32(dev, "linux,code", &fp->code);
	if (err)
		fp->code = KEY_HOMEPAGE;

	fp->input = devm_input_allocate_device(dev);
	if (!fp->input)
		return -ENOMEM;

	fp->input->name = GOODIX_FP_BUTTON_NAME;
	fp->input->phys = "goodix-fp/input0";
	fp->input->id.bustype = BUS_HOST;

	__set_bit(EV_KEY, fp->input->evbit);
	__set_bit(fp->code, fp->input->keybit);

	err = input_register_device(fp->input);
	if (err)
		return dev_err_probe(dev, err, "failed to register input device\n");

	gpiod_set_value_cansleep(fp->reset_gpio, 1);

	irq = gpiod_to_irq(fp->irq_gpio);
	if (irq < 0)
		return dev_err_probe(dev, irq, "failed to map irq GPIO to IRQ\n");

	err = devm_request_threaded_irq(dev, irq, NULL,
					goodix_fp_button_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT, GOODIX_FP_BUTTON_NAME, fp);
	if (err)
		return dev_err_probe(dev, err, "failed to request IRQ %d\n", irq);

	platform_set_drvdata(pdev, fp);

	dev_info(dev, "fingerprint button (code 0x%x)\n", fp->code);

	return 0;
}

static const struct of_device_id goodix_fp_button_of_match[] = {
	{ .compatible = "goodix,fp-button" },
	{ }
};
MODULE_DEVICE_TABLE(of, goodix_fp_button_of_match);

static struct platform_driver goodix_fp_button_driver = {
	.probe = goodix_fp_button_probe,
	.driver = {
		.name = GOODIX_FP_BUTTON_NAME,
		.of_match_table = goodix_fp_button_of_match,
	},
};

module_platform_driver(goodix_fp_button_driver);

MODULE_AUTHOR("Barnabas Czeman");
MODULE_DESCRIPTION("Goodix fingerprint sensor button driver");
MODULE_LICENSE("GPL-2.0");