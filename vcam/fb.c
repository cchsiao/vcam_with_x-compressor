#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/kmod.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

#include "fb.h"
#include "videobuf.h"
#include "libx.h"

loff_t pos;
struct layer {
    void *data;  /* input data */
    size_t size; /* input size */
} layer[2];

static int vcamfb_open(struct inode *ind, struct file *file)
{
    pr_info("[Jones][vcamfb_open]\n");
    unsigned long flags = 0;
    struct vcam_device *dev = PDE_DATA(ind);
    if (!dev) {
        pr_err("Private data field of PDE not initilized.\n");
        return -ENODEV;
    }

    dev->compressed_file = filp_open("/home/jones/Project/github/vcam/file_compressed", O_RDWR | O_CREAT, 0644);
    if (IS_ERR(dev->compressed_file)) {
        pr_info("create compressed file failed\n");
        return -1;
    }

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    if (dev->fb_isopen) {
        spin_unlock_irqrestore(&dev->in_fh_slock, flags);
        return -EBUSY;
    }
    dev->fb_isopen = true;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);

    file->private_data = dev;

    return 0;
}

static int vcamfb_release(struct inode *ind, struct file *file)
{
    pr_info("[Jones][vcamfb_release]\n");
    unsigned long flags = 0;
    struct vcam_device *dev = PDE_DATA(ind);

    filp_close(dev->compressed_file, NULL);

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    dev->fb_isopen = false;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);
    dev->in_queue.pending->filled = 0;
    return 0;
}

static ssize_t vcamfb_write(struct file *file,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    pr_info("[Jones][vcamfb_write]\n");
    struct vcam_in_queue *in_q;
    struct vcam_in_buffer *buf;
    size_t waiting_bytes;
    size_t to_be_copyied;
    unsigned long flags = 0;
    void *data;

    struct vcam_device *dev = file->private_data;
    if (!dev) {
        pr_err("Private data field of file not initialized yet.\n");
        return 0;
    }

    waiting_bytes = dev->input_format.sizeimage;

    in_q = &dev->in_queue;

    buf = in_q->pending;
    if (!buf) {
        pr_err("Pending pointer set to NULL\n");
        return 0;
    }

    /* Reset buffer if last write is too old */
    if (buf->filled && (((int32_t) jiffies - buf->jiffies) / HZ)) {
        pr_debug("Reseting jiffies, difference %d\n",
                 ((int32_t) jiffies - buf->jiffies));
        buf->filled = 0;
    }
    buf->jiffies = jiffies;

    /* Fill the buffer */
    /* TODO: implement real buffer handling */
    to_be_copyied = length;
    if ((buf->filled + to_be_copyied) > waiting_bytes)
        to_be_copyied = waiting_bytes - buf->filled;

    data = buf->data;
    if (!data) {
        pr_err("NULL pointer to framebuffer");
        return 0;
    }

    if (copy_from_user(data + buf->filled, (void *) buffer, to_be_copyied) !=
        0) {
        pr_warn("Failed to copy_from_user!");
    }
    buf->filled += to_be_copyied;

    /*Begin compress file*/
    //load_layer(0, istream);
    size_t j = 0;
    layer[j].size = buf->filled;
    if (!(layer[j].data = kmalloc(layer[j].size, GFP_KERNEL))) {
        pr_err("Out of memory\n");
        //abort();
        return 0;
    }
    /*
    if (fread(layer[j].data, 1, layer[j].size, stream) < layer[j].size)
    {
        pr_err("Error size\n");
        return;
    }
    */
    layer[j].data = data;
    pr_info("Input size: %d bytes\n", layer[j].size);

    //if ((layer[1].data = malloc(8 * layer[0].size)) == NULL)
    if ((layer[1].data = kmalloc(8 * layer[0].size, GFP_KERNEL)) == NULL)
    {
        pr_warn("Failed to kmalloc!");
        return -1;
    }

    //fprintf(stderr, "Compressing...\n");
    pr_info("Compressing...\n");
    x_init();

    void *end = x_compress(layer[0].data, layer[0].size, layer[1].data);
    layer[1].size = (char *) end - (char *) layer[1].data;

    //save_layer(1, ostream);
    j = 1;
    pr_info("Output size: %d bytes\n", layer[j].size);
    //if (fwrite(layer[j].data, 1, layer[j].size, stream) < layer[j].size)
    //    abort();
    mm_segment_t oldfs;
    int ret;
    oldfs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    ret = vfs_write(dev->compressed_file, layer[j].data, layer[j].size, &pos);
    pr_info("[Jones][vcamfb_write] ret: %d\n", ret);
    set_fs(oldfs);

    /*End compress file*/

    if (buf->filled == waiting_bytes) {
        spin_lock_irqsave(&dev->in_q_slock, flags);
        swap_in_queue_buffers(in_q);
        spin_unlock_irqrestore(&dev->in_q_slock, flags);
    }

    //kfree(layer[0].data);
    kfree(layer[1].data);

    return to_be_copyied;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static struct proc_ops vcamfb_fops = {
    .proc_open = vcamfb_open,
    .proc_release = vcamfb_release,
    .proc_write = vcamfb_write,
};
#else
static struct file_operations vcamfb_fops = {
    .owner = THIS_MODULE,
    .open = vcamfb_open,
    .release = vcamfb_release,
    .write = vcamfb_write,
};
#endif

struct proc_dir_entry *init_framebuffer(const char *proc_fname,
                                        struct vcam_device *dev)
{
    struct proc_dir_entry *procf;

    pr_debug("Creating framebuffer for /dev/%s\n", proc_fname);
    procf = proc_create_data(proc_fname, 0666, NULL, &vcamfb_fops, dev);
    if (!procf) {
        pr_err("Failed to create procfs entry\n");
        /* FIXME: report -ENODEV */
        goto failure;
    }

failure:
    return procf;
}

void destroy_framebuffer(const char *proc_fname)
{
    if (!proc_fname)
        return;

    pr_debug("Destroying framebuffer %s\n", proc_fname);
    remove_proc_entry(proc_fname, NULL);
}
