/*
 * capture.c
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/usb/composite.h>

#include "g_capture.h"
#include "sdebug.h"

USB_GADGET_COMPOSITE_OPTIONS();


#define DRIVER_VENDOR_NUM	0x0525		/* NetChip */
#define DRIVER_PRODUCT_NUM	0xa4a1		/* Linux-USB "Gadget Capture" */
#define DEFAULT_AUTORESUME	0

static const char longname[] = "Gadget Capture";
/* default serial number takes at least two packets */
static char serial[] = "0123456789.0123456789.0123456789";

static struct usb_capture_options gcapture_options = {
	.isoc_interval = 4,
	.isoc_maxpacket = 1024,
	.bulk_buflen = 4096,
	.qlen = 32,
};


static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,

	.idVendor =		cpu_to_le16(DRIVER_VENDOR_NUM),
	.idProduct =		cpu_to_le16(DRIVER_PRODUCT_NUM),
	.bNumConfigurations =	2,
};



#define USB_GADGET_CAPTURE_REAL_DESC	(USB_GADGET_FIRST_AVAIL_IDX + 0)
#define USB_GADGET_CAPTURE_FILE_DESC	(USB_GADGET_FIRST_AVAIL_IDX + 1)

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = longname,
	[USB_GADGET_SERIAL_IDX].s = serial,
	[USB_GADGET_CAPTURE_REAL_DESC].s	= "trans ethernet captured data real time",
	[USB_GADGET_CAPTURE_FILE_DESC].s	= "trans ethernet captured data from files",
	{  }			/* end of list */
};

static struct usb_function *func_cr;
static struct usb_function_instance *func_inst_cr;


static struct usb_function *func_cf;
static struct usb_function_instance *func_inst_cf;

/*static int cr_config_setup(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl)
{
	switch (ctrl->bRequest) {
	case 0x5b:
	case 0x5c:
		return func_ss->setup(func_ss, ctrl);
	default:
		return -EOPNOTSUPP;
	}
}*/
static struct usb_configuration capturereal_driver = {
	.label                  = "capturereal",
	//.setup                  = cr_config_setup,
	.bConfigurationValue    = 2,								/*上位机选择配置*/
	.bmAttributes           = USB_CONFIG_ATT_SELFPOWER,
	/* .iConfiguration      = DYNAMIC */
};

static struct usb_configuration capturfile_driver = {
	.label          		= "capturfile",
	.bConfigurationValue 	= 3,								/*上位机选择配置*/
	.bmAttributes   		= USB_CONFIG_ATT_SELFPOWER,
	/* .iConfiguration 		= DYNAMIC */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

//?
unsigned autoresume = DEFAULT_AUTORESUME;
static unsigned autoresume_step_ms;


/********************************inside func**************************************************/

static struct timer_list	autoresume_timer;
static void capture_autoresume(unsigned long _c)
{
	struct usb_composite_dev	*cdev = (void *)_c;
	struct usb_gadget		*g = cdev->gadget;

	/* unconfigured devices can't issue wakeups */
	if (!cdev->config)
		return;

	/* Normally the host would be woken up for something
	 * more significant than just a timer firing; likely
	 * because of some direct user request.
	 */
	if (g->speed != USB_SPEED_UNKNOWN) {
		int status = usb_gadget_wakeup(g);
		INFO(cdev, "%s --> %d\n", __func__, status);
	}
}
/********************** *****END inside func END**********************************************/

static int __init capture_bind(struct usb_composite_dev *cdev)
{
//!
	struct f_cr_opts	*cr_opts;
	struct f_cf_opts	*cf_opts;

	int			status;

	S_DEBUG("capture_bind IN\n");
	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */
	status = usb_string_ids_tab(cdev, strings_dev);	//给strings_dev[]的id字段赋值编号
	if (status < 0) {
		S_DEBUG("SSS:01\n");
		return status;
		}
	
	//下面三行就可以根据上一步得到的id来赋值索引值了
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;	/*供应商字符串*/
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;				/*产品字符串*/
	device_desc.iSerialNumber = strings_dev[USB_GADGET_SERIAL_IDX].id;			/*设备序列号字符串*/
	
	setup_timer(&autoresume_timer, capture_autoresume, (unsigned long) cdev);		/*参数列表:定时器名，超时处理函数，给超时处理函数的参数*/
	

	func_inst_cr = usb_get_function_instance("CaptureReal");						//不知道干了什么。。。。先套用
	if (IS_ERR(func_inst_cr)) {
		S_DEBUG("SSS:02\n");
		return PTR_ERR(func_inst_cr);
		}

	cr_opts =  container_of(func_inst_cr, struct f_cr_opts, func_inst);			/*返回了包含func_inst_ss的f_cr_opts类型结构体的首地址(可并没有事先把func_inst_cr赋给f_cr_opts中的func_inst成员啊。。。)*/
	/*尽管上一步有问题，不过这里应该还是ss_opts的func_inst已经指向func_inst_ss了
	 */
	cr_opts->pattern = gcapture_options.pattern;
	cr_opts->isoc_interval = gcapture_options.isoc_interval;
	cr_opts->isoc_maxpacket = gcapture_options.isoc_maxpacket;
	cr_opts->isoc_mult = gcapture_options.isoc_mult;
	cr_opts->isoc_maxburst = gcapture_options.isoc_maxburst;
	cr_opts->bulk_buflen = gcapture_options.bulk_buflen;

	func_cr = usb_get_function(func_inst_cr);									//不知道干了什么。。。。先套用
	if (IS_ERR(func_cr)) {
		S_DEBUG("SSS:03\n");
		status = PTR_ERR(func_cr);
		goto err_put_func_inst_cr;
	}

	func_inst_cf = usb_get_function_instance("CaptureFile");						/**/
	if (IS_ERR(func_inst_cf)) {
		S_DEBUG("SSS:04\n");
		status = PTR_ERR(func_inst_cf);
		goto err_put_func_cr;
	}

	cf_opts = container_of(func_inst_cf, struct f_cf_opts, func_inst);			/**/
	cf_opts->bulk_buflen = gcapture_options.bulk_buflen;
	cf_opts->qlen = gcapture_options.qlen;

	func_cf = usb_get_function(func_inst_cf);									/**/
	if (IS_ERR(func_cf)) {
		S_DEBUG("SSS:05\n");
		status = PTR_ERR(func_cf);
		goto err_put_func_inst_cf;
	}

	capturereal_driver.iConfiguration = strings_dev[USB_GADGET_CAPTURE_REAL_DESC].id;		/*给ss配置的字符串索引赋值*/
	capturfile_driver.iConfiguration = strings_dev[USB_GADGET_CAPTURE_FILE_DESC].id;		/*给lb配置的字符串索引赋值*/

//?
	/* support autoresume for remote wakeup testing */
	capturereal_driver.bmAttributes &= ~USB_CONFIG_ATT_WAKEUP;			/*失能远程唤醒*/
	capturfile_driver.bmAttributes &= ~USB_CONFIG_ATT_WAKEUP;			/*失能远程唤醒*/
	capturereal_driver.descriptors = NULL;
	capturfile_driver.descriptors = NULL;
	if (autoresume) {													/*远程唤醒配置*/
		capturereal_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
		capturfile_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
		autoresume_step_ms = autoresume * 1000;
	}

	usb_add_config_only(cdev, &capturfile_driver);						/*将config(capturfile_driver)加入到dev(cdev)中*/
	usb_add_config_only(cdev, &capturereal_driver);

	
	status = usb_add_function(&capturereal_driver, func_cr);			/*将function加入到config中*/
	if (status) {
		S_DEBUG("SSS:06\n");
		goto err_conf_flb;
		}
	usb_ep_autoconfig_reset(cdev->gadget);								/*设置端点*/
	
	status = usb_add_function(&capturfile_driver, func_cf);				/*将function加入到config中*/
	if (status) {
		S_DEBUG("SSS:07\n");
		goto err_conf_flb;
		}
	usb_ep_autoconfig_reset(cdev->gadget);								/*设置端点*/
	
	usb_composite_overwrite_options(cdev, &coverwrite);

	S_DEBUG("capture_bind OUT\n");

	return 0;

err_conf_flb:
	usb_put_function(func_cf);
	func_cf = NULL;
err_put_func_inst_cf:
	usb_put_function_instance(func_inst_cf);
	func_inst_cf = NULL;
err_put_func_cr:
	usb_put_function(func_cr);
	func_cf = NULL;
err_put_func_inst_cr:
	usb_put_function_instance(func_inst_cr);
	func_inst_cr = NULL;
	S_DEBUG("capture_bind ERROUT\n");
	return status;
}

static __refdata struct usb_composite_driver capture_driver = {
	.name		= "capture",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_SUPER,
	.bind		= capture_bind,
	//.unbind		= zero_unbind,
	//.suspend	= zero_suspend,
	//.resume		= zero_resume,
};

static int __init init(void)
{
	return usb_composite_probe(&capture_driver);
}
late_initcall(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&capture_driver);
}
module_exit(cleanup);

MODULE_AUTHOR("Soldier");
MODULE_LICENSE("GPL");
