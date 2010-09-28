/*
 * Setup platform devices needed by the Freescale multi-port host
 * and/or dual-role USB controller modules based on the description
 * in flat device tree.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_platform.h>

struct fsl_usb2_dev_data {
	char *dr_mode;		/* controller mode */
	char *drivers[3];	/* drivers to instantiate for this mode */
	enum fsl_usb2_operating_modes op_mode;	/* operating mode */
};

struct fsl_usb2_dev_data dr_mode_data[] __devinitdata = {
	{
		.dr_mode = "host",
		.drivers = { "fsl-ehci", NULL, NULL, },
		.op_mode = FSL_USB2_DR_HOST,
	},
	{
		.dr_mode = "otg",
		.drivers = { "fsl-usb2-otg", "fsl-ehci", "fsl-usb2-udc", },
		.op_mode = FSL_USB2_DR_OTG,
	},
	{
		.dr_mode = "peripheral",
		.drivers = { "fsl-usb2-udc", NULL, NULL, },
		.op_mode = FSL_USB2_DR_DEVICE,
	},
};

struct fsl_usb2_dev_data * __devinit get_dr_mode_data(struct device_node *np)
{
	const unsigned char *prop;
	int i;

	prop = of_get_property(np, "dr_mode", NULL);
	if (prop) {
		for (i = 0; i < ARRAY_SIZE(dr_mode_data); i++) {
			if (!strcmp(prop, dr_mode_data[i].dr_mode))
				return &dr_mode_data[i];
		}
	}
	pr_warn("%s: Invalid 'dr_mode' property, fallback to host mode\n",
		np->full_name);
	return &dr_mode_data[0]; /* mode not specified, use host */
}

static enum fsl_usb2_phy_modes __devinit determine_usb_phy(const char *phy_type)
{
	if (!phy_type)
		return FSL_USB2_PHY_NONE;
	if (!strcasecmp(phy_type, "ulpi"))
		return FSL_USB2_PHY_ULPI;
	if (!strcasecmp(phy_type, "utmi"))
		return FSL_USB2_PHY_UTMI;
	if (!strcasecmp(phy_type, "utmi_wide"))
		return FSL_USB2_PHY_UTMI_WIDE;
	if (!strcasecmp(phy_type, "serial"))
		return FSL_USB2_PHY_SERIAL;

	return FSL_USB2_PHY_NONE;
}

struct platform_device * __devinit fsl_usb2_device_register(
					struct platform_device *ofdev,
					struct fsl_usb2_platform_data *pdata,
					const char *name, int id)
{
	struct platform_device *pdev;
	const struct resource *res = ofdev->resource;
	unsigned int num = ofdev->num_resources;
	int retval;

	pdev = platform_device_alloc(name, id);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	pdev->dev.parent = &ofdev->dev;

	pdev->dev.coherent_dma_mask = ofdev->dev.coherent_dma_mask;
	pdev->dev.dma_mask = &pdev->archdata.dma_mask;
	*pdev->dev.dma_mask = *ofdev->dev.dma_mask;

	retval = platform_device_add_data(pdev, pdata, sizeof(*pdata));
	if (retval)
		goto error;

	if (num) {
		retval = platform_device_add_resources(pdev, res, num);
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}

static const struct of_device_id fsl_usb2_mph_dr_of_match[];

static int __devinit fsl_usb2_mph_dr_of_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct platform_device *usb_dev;
	struct fsl_usb2_platform_data data, *pdata;
	struct fsl_usb2_dev_data *dev_data;
	const struct of_device_id *match;
	const unsigned char *prop;
	static unsigned int idx;
	int i;

	if (!of_device_is_available(np))
		return -ENODEV;

	match = of_match_device(fsl_usb2_mph_dr_of_match, &ofdev->dev);
	if (!match)
		return -ENODEV;

	pdata = &data;
	if (match->data)
		memcpy(pdata, match->data, sizeof(data));
	else
		memset(pdata, 0, sizeof(data));

	dev_data = get_dr_mode_data(np);

	if (of_device_is_compatible(np, "fsl-usb2-mph")) {
		if (of_get_property(np, "port0", NULL))
			pdata->port_enables |= FSL_USB2_PORT0_ENABLED;

		if (of_get_property(np, "port1", NULL))
			pdata->port_enables |= FSL_USB2_PORT1_ENABLED;

		pdata->operating_mode = FSL_USB2_MPH_HOST;
	} else {
		/* setup mode selected in the device tree */
		pdata->operating_mode = dev_data->op_mode;
	}

	prop = of_get_property(np, "phy_type", NULL);
	pdata->phy_mode = determine_usb_phy(prop);

	for (i = 0; i < ARRAY_SIZE(dev_data->drivers); i++) {
		if (!dev_data->drivers[i])
			continue;
		usb_dev = fsl_usb2_device_register(ofdev, pdata,
					dev_data->drivers[i], idx);
		if (IS_ERR(usb_dev)) {
			dev_err(&ofdev->dev, "Can't register usb device\n");
			return PTR_ERR(usb_dev);
		}
	}
	idx++;
	return 0;
}

static int __devexit __unregister_subdev(struct device *dev, void *d)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int __devexit fsl_usb2_mph_dr_of_remove(struct platform_device *ofdev)
{
	device_for_each_child(&ofdev->dev, NULL, __unregister_subdev);
	return 0;
}

static const struct of_device_id fsl_usb2_mph_dr_of_match[] = {
	{ .compatible = "fsl-usb2-mph", },
	{ .compatible = "fsl-usb2-dr", },
	{},
};

static struct platform_driver fsl_usb2_mph_dr_driver = {
	.driver = {
		.name = "fsl-usb2-mph-dr",
		.owner = THIS_MODULE,
		.of_match_table = fsl_usb2_mph_dr_of_match,
	},
	.probe	= fsl_usb2_mph_dr_of_probe,
	.remove	= __devexit_p(fsl_usb2_mph_dr_of_remove),
};

static int __init fsl_usb2_mph_dr_init(void)
{
	return platform_driver_register(&fsl_usb2_mph_dr_driver);
}
module_init(fsl_usb2_mph_dr_init);

static void __exit fsl_usb2_mph_dr_exit(void)
{
	platform_driver_unregister(&fsl_usb2_mph_dr_driver);
}
module_exit(fsl_usb2_mph_dr_exit);

MODULE_DESCRIPTION("FSL MPH DR OF devices driver");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_LICENSE("GPL");
