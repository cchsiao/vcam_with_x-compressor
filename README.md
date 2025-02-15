# vcam_with_x-compressor

## How to integate x-compressor into vcam?

* Understand the usage of `vcam` and `x-compressor`.
* Trace the code of `vcam` and `x-compressor`.

## For x-compressor

* Find out the core compressed function of `x-compressor`
* Modify the `libx.c` and `libx.h` for kernel building

## For vcam

* Add the new variable `struct file *compressed_file` for compressed file in `struct vcam_device`
* Add the symbolic link to reference `libx.c` and `libx.h` in `x-compressor`

## The workflow for vcam output the compressed data

* Figure out the input of data stream
* Open the compressed file `file_compressed` in `vcamfb_open`
* Compress the data and write to `file_compressed` in `vcamfb_write`
* Close `file_compressed` in `vcamfb_release`

## How to build?

```shel
$ make
```

## How to use?

After loading the kernel module for vcam, we have to run the command(X: the index of device):
```shell
$ cat triathlon.jpg > /proc/vcamfbX
```
And we will get `file_compressed`

## How to test?

After decompressing the `file_compressed` by `unx`, and we will get the decompressed file which is same as triathlon.jpg

## Know issue
The execution command will be crashed if the file size is big.
