#ifndef __NVME_HINT
#define __NVME_HINT
#include <fcntl.h>
#include <libnvme.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <nvme-hint.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "buf0buf.h"

void nvme_send_buffer_clean(buf_page_t *bpage);

void nvme_send_buffer_dirty(buf_page_t *bpage);

void nvme_send_buffer_evicted(buf_page_t *bpage);

#endif