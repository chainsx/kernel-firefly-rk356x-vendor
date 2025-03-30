/*
 * Driver for quectel L76K GPS on Firefly board.
 *
 * Copyright (C) 2016, Zhongshan T-chip Intelligent Technology Co.,ltd.
 * Copyright 2006  JC.Lin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

enum l76k_status {
    L76K_INIT,
    L76K_FULL_ON,
    L76K_STANDBY,
    L76K_BACKUP
};

struct firefly_l76k_info {
    int pwr_gpio;
    int pwr_gpio_value;
    int reset_gpio;
    int reset_gpio_value;
    int standby_gpio;
    int standby_gpio_value;
    enum l76k_status mode;
    enum l76k_status last_mode;
};

static struct firefly_l76k_info * l76k_info;

static int firefly_l76k_of_node_parse(struct device *dev)
{
    int gpio;
    enum of_gpio_flags flag;
    struct device_node *node = dev->of_node;

    gpio = of_get_named_gpio_flags(node, "pwr-gpio", 0, &flag);
    if (!gpio_is_valid(gpio)) {
        dev_err(dev, "pwr-gpio: %d is invalid\n", gpio);
        return -ENODEV;
    }
    if (gpio_request(gpio, "pwr-gpio")) {
        dev_err(dev, "pwr-gpio: %d request failed!\n", gpio);
        gpio_free(gpio);
        return -ENODEV;
    }
    l76k_info->pwr_gpio = gpio;
    l76k_info->pwr_gpio_value = (flag == OF_GPIO_ACTIVE_LOW) ? 0:1;

    gpio = of_get_named_gpio_flags(node, "reset-gpio", 0, &flag);
    if (!gpio_is_valid(gpio)) {
        dev_err(dev, "reset-gpio: %d is invalid\n", gpio);
        return -ENODEV;
    }
    if (gpio_request(gpio, "reset-gpio")) {
        dev_err(dev, "reset-gpio: %d request failed!\n", gpio);
        gpio_free(gpio);
        return -ENODEV;
    }
    l76k_info->reset_gpio = gpio;
    l76k_info->reset_gpio_value = (flag == OF_GPIO_ACTIVE_LOW) ? 0:1;

    gpio = of_get_named_gpio_flags(node, "standby-gpio", 0, &flag);
    if (!gpio_is_valid(gpio)) {
        dev_err(dev, "standby-gpio: %d is invalid\n", gpio);
        return -ENODEV;
    }
    if (gpio_request(gpio, "standby-gpio")) {
        dev_err(dev, "standby-gpio: %d request failed!\n", gpio);
        gpio_free(gpio);
        return -ENODEV;
    }
    l76k_info->standby_gpio = gpio;
    l76k_info->standby_gpio_value = (flag == OF_GPIO_ACTIVE_LOW) ? 0:1;

    return 0;
}

static void __l76k_reset(void)
{
    l76k_info->reset_gpio_value = 1;
    gpio_direction_output(l76k_info->reset_gpio, l76k_info->reset_gpio_value);
    msleep(15);
    l76k_info->reset_gpio_value = 0;
    gpio_direction_output(l76k_info->reset_gpio, l76k_info->reset_gpio_value);
}

static void __l76k_init(void)
{
    l76k_info->pwr_gpio_value = 1;
    gpio_direction_output(l76k_info->pwr_gpio, l76k_info->pwr_gpio_value);
    l76k_info->standby_gpio_value = 0;
    gpio_direction_output(l76k_info->standby_gpio, l76k_info->standby_gpio_value);
    l76k_info->reset_gpio_value = 0;
    gpio_direction_output(l76k_info->reset_gpio, l76k_info->reset_gpio_value);
    __l76k_reset();
    l76k_info->last_mode = L76K_FULL_ON;
    l76k_info->mode = L76K_FULL_ON;
}

static void __l76k_backup(void)
{
    l76k_info->pwr_gpio_value = 0;
    gpio_direction_output(l76k_info->pwr_gpio, l76k_info->pwr_gpio_value);
    l76k_info->standby_gpio_value = 1;
    gpio_direction_output(l76k_info->standby_gpio, l76k_info->standby_gpio_value);
    l76k_info->reset_gpio_value = 1;
    gpio_direction_output(l76k_info->reset_gpio, l76k_info->reset_gpio_value);
    l76k_info->last_mode = l76k_info->mode;
}

static void __l76k_full_on(void)
{
    l76k_info->pwr_gpio_value = 1;
    gpio_direction_output(l76k_info->pwr_gpio, l76k_info->pwr_gpio_value);
    l76k_info->standby_gpio_value = 0;
    gpio_direction_output(l76k_info->standby_gpio, l76k_info->standby_gpio_value);
    l76k_info->reset_gpio_value = 0;
    gpio_direction_output(l76k_info->reset_gpio, l76k_info->reset_gpio_value);
    l76k_info->last_mode = l76k_info->mode;
}

static void __l76k_standby(void)
{
    l76k_info->pwr_gpio_value = 1;
    gpio_direction_output(l76k_info->pwr_gpio, l76k_info->pwr_gpio_value);
    l76k_info->standby_gpio_value = 1;
    gpio_direction_output(l76k_info->standby_gpio, l76k_info->standby_gpio_value);
    l76k_info->reset_gpio_value = 0;
    gpio_direction_output(l76k_info->reset_gpio, l76k_info->reset_gpio_value);
    l76k_info->last_mode = l76k_info->mode;
}

static void firefly_l76k_status_switch(void)
{
    if(l76k_info->last_mode == l76k_info->mode)
        return;

    switch (l76k_info->mode) {
        case L76K_FULL_ON :
            __l76k_full_on();
            break;
        case L76K_STANDBY :
            __l76k_standby();
            break;
        case L76K_BACKUP :
            __l76k_backup();
            break;
        default :
            break;
    }
}

static void firefly_set_l76k_mode(enum l76k_status mode)
{
    if(mode == L76K_INIT) {
        l76k_info->mode = L76K_INIT;
        __l76k_init();
    }
    else {
        l76k_info->last_mode = l76k_info->mode;
        l76k_info->mode = mode;
        firefly_l76k_status_switch();
    }
}

static ssize_t l76k_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "l76k_mode:%d\n",l76k_info->mode);
}

static ssize_t l76k_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char number;
    enum l76k_status mode;
    strncpy(&number, buf, 1);
    // sscanf(buf, "%c", &number);
    switch (number-48) {
        case 1 :
            mode = L76K_FULL_ON;
            break;
        case 2 :
            mode = L76K_STANDBY;
            break;
        case 3 :
            mode = L76K_BACKUP;
            break;
        default :
            break;
    }
    firefly_set_l76k_mode(mode);
    return count;
}

static DEVICE_ATTR(l76k_mode, 0664, l76k_mode_show, l76k_mode_store);

static struct attribute *l76k_attributes[] = {
    &dev_attr_l76k_mode.attr,
    NULL,
};

static const struct attribute_group l76k_attrs = {
    .attrs = l76k_attributes,
};

static int firefly_l76k_probe(struct platform_device *pdev)
{
    int ret;
    struct device *dev = &pdev->dev;

    printk("Firefly l76k Probe\n");
    l76k_info = devm_kzalloc(dev, sizeof(struct firefly_l76k_info), GFP_KERNEL);
    if (!l76k_info) {
        dev_err(dev, "devm_kzalloc firefly_l76k_info failed!\n");
        return -ENOMEM;
    }
    dev_set_drvdata(dev,l76k_info);

    ret = firefly_l76k_of_node_parse(dev);
    if(ret) {
        dev_err(dev, "device tree parse failed!\n");
        return ret;
    }

    firefly_set_l76k_mode(L76K_INIT);
    ret = sysfs_create_group(&pdev->dev.kobj, &l76k_attrs);

    printk("Firefly l76k Finish\n");
    return 0;
}

static struct of_device_id firefly_match_table[] = {
    { .compatible = "quectel,l76k",},
    {},
};

static struct platform_driver firefly_l76k_driver = {
    .driver = {
            .name = "firefly-l76k",
            .owner = THIS_MODULE,
            .of_match_table = firefly_match_table,
    },
    .probe = firefly_l76k_probe,
};

static int firefly_l76k_init(void)
{
    return platform_driver_register(&firefly_l76k_driver);
}
module_init(firefly_l76k_init);

static void firefly_l76k_exit(void)
{
    platform_driver_unregister(&firefly_l76k_driver);
}
module_exit(firefly_l76k_exit);

MODULE_AUTHOR("maocl <service@t-firefly.com>");
MODULE_DESCRIPTION("Firefly L76K driver");
MODULE_LICENSE("GPL");
