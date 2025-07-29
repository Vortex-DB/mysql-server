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
#include <memory>
#include <vector>

#include "buf0buf.h"

/** Initialize NVMe hint mapping tables. */
void nvme_map_init();

/** Free NVMe hint mapping tables. */
void nvme_map_free();

void nvme_clear_mapping(buf_page_t *bpage);

void nvme_set_mapping(buf_page_t *bpage);

int64_t nvme_get_sector_number(buf_page_t *bpage);

buf_page_t *nvme_get_bpage(uint64_t sector);

void nvme_send_buffer_clean(buf_page_t *bpage);

void nvme_send_buffer_dirty(buf_page_t *bpage);

void nvme_send_buffer_evicted(buf_page_t *bpage);

struct NVMe_Reclaim_Info {
  // # of distint pages
  uint32_t num_pages;

  // starting sectors
  std::vector<uint64_t> sectors;
};

std::shared_ptr<NVMe_Reclaim_Info> nvme_get_next_reclaim_info();

#endif