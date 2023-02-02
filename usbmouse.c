#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include<linux/fs.h>
//Device init
#include<linux/device.h>
#include<linux/uaccess.h>
#include"character_driver.h"
#include<linux/cdev.h>
#include <linux/sched.h> 


static char current_data = 0;
typedef struct vchar_dev
{
	unsigned char* control_regs;
	unsigned char* status_regs;
	unsigned char* data_regs;
}vchar_dev_t;

struct _vchar_drv
{
	dev_t dev_number;
	struct class *dev_class;
	struct device *dev;
	vchar_dev_t* vchar_hw;
	struct cdev *vcdev;
}vchar_drv;
int vchar_hw_init(vchar_dev_t*hw)
{
	char*buf;
	buf=kzalloc(NUM_DEV_REGS*REG_SIZE, GFP_KERNEL);
	if(!buf)
	{
		return -ENOMEM;
	}
	hw->control_regs=buf;
	hw->status_regs=hw->control_regs + NUM_CTRL_REGS;
	hw->data_regs=hw->status_regs + NUM_STS_REGS;
	
	//KHOI TAO GIA TRI THANH GHI//
	hw->control_regs[CONTROL_ACCESS_REG] = 0x04;
	hw->status_regs[DEVICE_STATUS_REG]=0x04;
	return 0;
}
void vchar_hw_exit(vchar_dev_t*hw)
{
	kfree(hw->control_regs);
}

///CAc ham entry POINT
static int dev_open(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "character_driver: Device is opening....\n");
    return 0;
}

static struct input_dev *myusb_kbd_dev;           //input_dev
static unsigned char *myusb_kbd_buf;                //Virtual address buffer
static dma_addr_t myusb_kbd_phyc;                  //DMA buffer area;

static __le16 myusb_kbd_size;                            //Packet length
static struct urb  *myusb_kbd_urb;                     //urb

static const unsigned char usb_kbd_keycode[252] = {
         0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
        50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
         4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
        27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
        65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
       105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
        72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
       191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
       115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
       122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
       150,158,159,128,136,177,178,176,142,152,173,140
};       //The keyboard code table has a total of 252 data

 
void my_memcpy(unsigned char *dest,unsigned char *src,int len)      //Copy cache
{
       while(len--)
        {
            *dest++= *src++;
        }
}

static void myusb_kbd_irq(struct urb *urb)               //Keyboard interrupt function
{
   static unsigned char buf1[8]={0,0,0,0,0,0,0,0};
   int i;
   //struct usb_kbd *kbd = urb->context;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		return;
	}

      /*Upload crtl, shift, atl, windows and other buttons*/
     for (i = 0; i < 8; i++)
     if(((myusb_kbd_buf[0]>>i)&1)!=((buf1[0]>>i)&1))
     {    
             input_report_key(myusb_kbd_dev, usb_kbd_keycode[i + 224], (myusb_kbd_buf[0]>> i) & 1);
             input_sync(myusb_kbd_dev);             //Upload synchronization event
      }


     /*Upload normal button*/
    for(i=2;i<8;i++)
    if(myusb_kbd_buf[i]!=buf1[i])
    {
     if(myusb_kbd_buf[i] )  {    //Press event
    input_report_key(myusb_kbd_dev,usb_kbd_keycode[myusb_kbd_buf[i]], 1);   
    printk("Press : %x", usb_kbd_keycode[myusb_kbd_buf[i]]); 
    current_data = usb_kbd_keycode[myusb_kbd_buf[i]];
       }
    else  if(buf1[i])   {                                          //Release event
    input_report_key(myusb_kbd_dev,usb_kbd_keycode[buf1[i]], 0);
    input_sync(myusb_kbd_dev); }            //Upload synchronization event
    
    }

  my_memcpy(buf1, myusb_kbd_buf, 8);       //update data    
  usb_submit_urb(myusb_kbd_urb, GFP_KERNEL);
}
static ssize_t dev_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset)
{

	copy_to_user(buffer, &current_data,1);
	return current_data;
}
static int dev_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "character_driver: Device successfully closed\n");
    return 0;
}
static struct file_operations fops =
{
        .open = dev_open,
        .read = dev_read,
        .release = dev_release,
};

static int myusb_kbd_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
       volatile unsigned char  i;
       struct usb_device *dev = interface_to_usbdev(intf);                 //equipment
       struct usb_endpoint_descriptor *endpoint;                            
       struct usb_host_interface *interface;                                              //Current interface
       int pipe;                                                                               //Endpoint pipeline
       interface=intf->cur_altsetting;                                                                   
       endpoint = &interface->endpoint[0].desc;                                    //The endpoint descriptor under the current interface
       printk("VID=%x,PID=%x\n",dev->descriptor.idVendor,dev->descriptor.idProduct);     

 /*      1) Allocate an input_dev structure  */
       myusb_kbd_dev=input_allocate_device();

 /*      2) Set input_dev to support key events*/
       set_bit(EV_KEY, myusb_kbd_dev->evbit);
       set_bit(EV_REP, myusb_kbd_dev->evbit);        //Support repeat press function

       for (i = 0; i < 252; i++)
       set_bit(usb_kbd_keycode[i], myusb_kbd_dev->keybit);     //Add all keys
       clear_bit(0, myusb_kbd_dev->keybit);

 /*      3) Register the input_dev structure*/
       if(input_register_device(myusb_kbd_dev)==1){}

 /*      4) Set up USB keyboard data transfer */
 /*->4.1) Create an endpoint pipe via usb_rcvintpipe()*/
       pipe=usb_rcvintpipe(dev,endpoint->bEndpointAddress); 

  /*->4.2) Apply for USB buffer through usb_buffer_alloc()*/
      myusb_kbd_size=endpoint->wMaxPacketSize;
      			  myusb_kbd_buf = usb_alloc_coherent(dev,myusb_kbd_size,GFP_ATOMIC,&myusb_kbd_phyc);

  /*->4.3) Apply and initialize the urb structure through usb_alloc_urb() and usb_fill_int_urb() */
       myusb_kbd_urb=usb_alloc_urb(0,GFP_KERNEL);
       usb_fill_int_urb (myusb_kbd_urb,              //urb structure
                                 dev,                                       //usb device
                                 pipe,                                      //Endpoint pipeline
                                 myusb_kbd_buf,               //Buffer address
                                 myusb_kbd_size,              //Data length
                                 myusb_kbd_irq,               //Interrupt function
                                 0,
                                 endpoint->bInterval);              //Interruption time
 
  /*->4.4) Because our 2440 supports DMA, we need to tell the urb structure to use the DMA buffer address*/
        myusb_kbd_urb->transfer_dma   =myusb_kbd_phyc;                  //Set DMA address
        myusb_kbd_urb->transfer_flags   =URB_NO_TRANSFER_DMA_MAP;     //Set to use DMA address

  /*->4.5) Use usb_submit_urb() to submit urb*/
        usb_submit_urb(myusb_kbd_urb, GFP_KERNEL);   
       return 0;
}

static void myusb_kbd_disconnect(struct usb_interface *intf)
{
    struct usb_device *dev = interface_to_usbdev(intf);        //equipment
    usb_kill_urb(myusb_kbd_urb);
    usb_free_urb(myusb_kbd_urb);
    usb_free_coherent(dev, myusb_kbd_size, myusb_kbd_buf,myusb_kbd_phyc);
    input_unregister_device(myusb_kbd_dev);               //Log out of input_dev in the kernel
    input_free_device(myusb_kbd_dev);                        //Release input_dev
}

static struct usb_device_id myusb_kbd_id_table [] = {
       { USB_INTERFACE_INFO(
              USB_INTERFACE_CLASS_HID,                      //Interface class: hid class
              USB_INTERFACE_SUBCLASS_BOOT,             //Subclass: start device class
              USB_INTERFACE_PROTOCOL_KEYBOARD) }, //USB protocol: keyboard protocol
};

static struct usb_driver myusb_kbd_drv = {
       .name            = "myusb_kbd",
       .probe           = myusb_kbd_probe,                        
       .disconnect     = myusb_kbd_disconnect,
       .id_table  = myusb_kbd_id_table,
};

/*Entry function*/
static int myusb_kbd_init(void)
{ 
//cap nhat device number//
	int ret=0;
	vchar_drv.dev_number = 0;
	ret = alloc_chrdev_region(&vchar_drv.dev_number,0, 1, "character_device");
	if(ret<0)
	{
		printk("Failed to register device number. \n");
		return ret;
	}
	printk("Allocated device number (%d,%d) \n", MAJOR(vchar_drv.dev_number),MINOR(vchar_drv.dev_number));
	//tao device file//
	vchar_drv.dev_class = class_create(THIS_MODULE,"class_vchar_dev");
	if(vchar_drv.dev_class==NULL)
	{
		printk("Failed to create device file");
		unregister_chrdev_region(vchar_drv.dev_number, 1);
		return 0;
	}
	vchar_drv.dev = device_create(vchar_drv.dev_class, NULL, vchar_drv.dev_number, NULL, "MyDeviceFile");
	if(IS_ERR(vchar_drv.dev))
	{
		printk("Failed to create device file");
		class_destroy(vchar_drv.dev_class);
		return 0;
	}
	printk("Device file has been created");

	//cap phat bo nho cau truc du lieu cua driver va khoi tao//
	vchar_drv.vchar_hw=kzalloc(sizeof(vchar_dev_t), GFP_KERNEL);
	if(!vchar_drv.vchar_hw){
		printk("Failed to allocate data structure of the device\n");
		ret=-ENOMEM;
		device_destroy(vchar_drv.dev_class, vchar_drv.dev_number);
		return 0;
	}
	printk("Data structure allocated successfully");


	//khoi tao thiet bi vat ly
	ret=vchar_hw_init(vchar_drv.vchar_hw);
	if(ret<0)
	{
		printk("Failed to initialize a virtual character device");
		kfree(vchar_drv.vchar_hw);
		return 0;
	}
	printk("Virtual character device initialized successfully");
	//dang ky cac entry point//
	vchar_drv.vcdev=cdev_alloc();
	if(vchar_drv.vcdev==NULL){
		printk("Failed to allocate cdev structure");
		vchar_hw_exit(vchar_drv.vchar_hw);
		return 0;
	}
	cdev_init(vchar_drv.vcdev, &fops);
	ret=cdev_add(vchar_drv.vcdev, vchar_drv.dev_number, 1);
	if(ret<0){
		printk("Failed to add entry point!");
		vchar_hw_exit(vchar_drv.vchar_hw);
		return 0;
	}
	printk("Entry points added succesfully");
	
	//dang ky ham xu ly ngat//
	
	usb_register(&myusb_kbd_drv);
	printk("Itnitialized character driver successully \n");
	
	return 0;
}
static void myusb_kbd_exit(void)
{

//huy dang ky entry point vs kernel//
	cdev_del(vchar_drv.vcdev);

	//giai phong thiet bi vat ly//
	vchar_hw_exit(vchar_drv.vchar_hw);	



	//giai phong bu nho da cap phat cau truc du lieu cua driver//
	kfree(vchar_drv.vchar_hw);

	//xoa bo device file//
	device_destroy(vchar_drv.dev_class, vchar_drv.dev_number);
	class_destroy(vchar_drv.dev_class);
	

	//giai phong device number//
	unregister_chrdev_region(vchar_drv.dev_number, 1);



	printk("Character_driver exited\n");
       usb_deregister(&myusb_kbd_drv);
}


module_init(myusb_kbd_init);
module_exit(myusb_kbd_exit);
MODULE_LICENSE("GPL");



--u_test--
	
	
	
	#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/select.h>
#include<sys/time.h>
#include<fcntl.h>
#include<errno.h>

#include <assert.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>


#define DEVICE_NODE "/dev/MyDeviceFile"



#define BUFFER_LENGTH 4            
int main(int argc, char**argv)
{
   unsigned int ret, fd=0;
   unsigned int result;

   fd = open(DEVICE_NODE, O_RDONLY); 
   if (fd < 0)
   {
	   return 0;  	
   }
   else{

	printf("Device file opened!\n");
	printf("Reading from the device...\n");
	while(1)
	{
   	ret = read(fd, &result, sizeof(BUFFER_LENGTH));
	if(ret == 30)
	{	
	    Display *disp;
	    Window root;
	    cairo_surface_t *surface;
	    int scr;
	    /* The only checkpoint only concerns about the number of parameters, see "Usage" */

	    /* try to connect to display, exit if it's NULL */
	    disp = XOpenDisplay( argv[1] );
	    if( disp == NULL ){
	    	fprintf(stderr, "Given display cannot be found, exiting: %s\n" , argv[1]);
	    	return 1;    	
	    }
	    scr = DefaultScreen(disp);
	    root = DefaultRootWindow(disp);
	    /* get the root surface on given display */
	    surface = cairo_xlib_surface_create(disp, root, DefaultVisual(disp, scr),
		                                            DisplayWidth(disp, scr), 
		                                            DisplayHeight(disp, scr));
	    /* right now, the tool only outputs PNG images */
	    cairo_surface_write_to_png( surface, argv[2] );
	    /* free the memory*/
	    cairo_surface_destroy(surface);
	    /* return with success */
	    close(fd); /////CLOSE the device
   	    printf("Good bye!\n");
            return 0;
	}

   	}
   	}

   return 0;
}


--character_driver.h----
	
	/*
 * ten file: vchar_driver.h
 * tac gia : dat.a3cbq91@gmail.com
 * ngay tao: 9/12/2018
 * mo ta   : chua cac dinh nghia mo ta thiet bi gia lap vchar_dev.
 *           vchar_device la mot thiet bi nam tren RAM.
 */

#define REG_SIZE 1 //kich thuoc cua 1 thanh ghi la 1 byte (8 bits)
#define NUM_CTRL_REGS 1 //so thanh ghi dieu khien cua thiet bi
#define NUM_STS_REGS 5 //so thanh ghi trang thai cua thiet bi
#define NUM_DATA_REGS 256 //so thanh ghi du lieu cua thiet bi
#define NUM_DEV_REGS (NUM_CTRL_REGS + NUM_STS_REGS + NUM_DATA_REGS) //tong so thanh ghi cua thiet bi

/****************** Mo ta cac thanh ghi trang thai: START ******************/
/*
 * cap thanh ghi [READ_COUNT_H_REG:READ_COUNT_L_REG]:
 * - gia tri luc khoi tao: 0x0000
 * - moi lan doc thanh cong tu cac thanh ghi du lieu, tang them 1 don vi
 */
#define READ_COUNT_H_REG 0
#define READ_COUNT_L_REG 1

/*
 * cap thanh ghi [WRITE_COUNT_H_REG:WRITE_COUNT_L_REG]:
 * - gia tri luc khoi tao: 0x0000
 * - moi lan ghi thanh cong vao cac thanh ghi du lieu, tang them 1 don vi
 */
#define WRITE_COUNT_H_REG 2
#define WRITE_COUNT_L_REG 3

/*
 * thanh ghi DEVICE_STATUS_REG:
 * - gia tri luc khoi tao: 0x03
 * - y nghia cua cac bit:
 *   bit0:
 *       0: cho biet cac thanh ghi du lieu dang khong san sang de doc
 *       1: cho biet cac thanh ghi du lieu da san sang cho viec doc
 *   bit1:
 *       0: cho biet cac thanh ghi du lieu dang khong san sang de ghi
 *       1: cho biet cac thanh ghi du lieu da san sang cho viec ghi
 *   bit2:
 *       0: khi cac thanh ghi du lieu bi xoa, bit nay se duoc thiet lap bang 0
 *       1: khi toan bo cac thanh ghi du lieu da bi ghi, bit nay se duoc thiet lap bang 1
 *   bit3~7: chua dung toi
 */
#define DEVICE_STATUS_REG 4

#define STS_READ_ACCESS_BIT (1 << 0)
#define STS_WRITE_ACCESS_BIT (1 << 1)
#define STS_DATAREGS_OVERFLOW_BIT (1 << 2)

#define READY 1
#define NOT_READY 0
#define OVERFLOW 1
#define NOT_OVERFLOW 0
/****************** Mo ta cac thanh ghi trang thai: END ******************/


/****************** Mo ta cac thanh ghi dieu khien: START ******************/
/*
 * thanh ghi CONTROL_ACCESS_REG:
 * - vai tro: chua cac bit dieu khien kha nang doc/ghi cac thanh ghi du lieu
 * - gia tri luc khoi tao: 0x03
 * - y nghia:
 *   bit0:
 *       0: khong cho phep doc tu cac thanh ghi du lieu
 *       1: cho phep doc tu cac thanh ghi du lieu
 *   bit1:
 *       0: khong cho phep ghi vao cac thanh ghi du lieu
 *       1: cho phep ghi vao cac thanh ghi du lieu
 *   bit2~7: chua dung toi
 */
#define CONTROL_ACCESS_REG 0

#define CTRL_READ_DATA_BIT (1 << 0)
#define CTRL_WRITE_DATA_BIT (1 << 1)

#define ENABLE 1
#define DISABLE 0

--Makefile---
	
	
	obj-m += usb_kb.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
