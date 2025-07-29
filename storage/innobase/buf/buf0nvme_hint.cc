#include "buf0nvme_hint.h"

#include <mutex>
#include <unordered_map>

#include "buf0buf.h"
#include "fil0fil.h"
#include "srv0srv.h"

std::mutex p2s_mutex;
std::mutex s2p_mutex;
std::unordered_map<buf_page_t *, uint64_t> page2sector;
std::unordered_map<uint64_t, buf_page_t *> sector2page;

void nvme_map_init() {
  std::scoped_lock lock{p2s_mutex, s2p_mutex};
  page2sector.clear();
  sector2page.clear();
}

void nvme_map_free() {
  std::scoped_lock lock{p2s_mutex, s2p_mutex};
  page2sector.clear();
  sector2page.clear();
}

struct fiemap *get_fiemap(int fd, uint64_t offset, uint64_t size) {
  struct fiemap *fiemap = (struct fiemap *)calloc(
      1, sizeof(struct fiemap) + sizeof(struct fiemap_extent));
  if (fiemap == NULL) {
    return NULL;
  }
  fiemap->fm_start = offset;
  fiemap->fm_length = size;
  fiemap->fm_extent_count = 1;
  fiemap->fm_flags = FIEMAP_FLAG_SYNC;

  if (ioctl(fd, FS_IOC_FIEMAP, fiemap)) {
    free(fiemap);
    fiemap = NULL;
  }
  return fiemap;
}

int64_t nvme_get_sector_number(buf_page_t *bpage) {
  if (!page2sector.contains(bpage)) {
    return -1LL;
  }
  return page2sector[bpage];
}

buf_page_t *nvme_get_bpage(uint64_t sector) {
  if (!sector2page.contains(sector)) {
    return nullptr;
  }
  return sector2page[sector];
}

void nvme_set_mapping(buf_page_t *bpage) {
  if (!srv_use_nvme_hint) {
    return;
  }
  if (bpage == nullptr || !buf_page_in_file(bpage)) {
    return;
  }
  fil_space_t *space = fil_space_get(bpage->id.space());

  if (space == nullptr) {
    return;
  }

  const char *path = space->name;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return;
  }

  const ulint page_size_phys = bpage->size.physical();
  const off_t offset = (off_t)bpage->id.page_no() * page_size_phys;

  struct fiemap *map = get_fiemap(fd, offset, page_size_phys);
  if (map == NULL) {
    return;
  }

  uint64_t p_offset = map->fm_extents[0].fe_physical;
  uint64_t sector_size = 512;
  uint64_t sector = p_offset / sector_size;
  free(map);

  std::scoped_lock lock(p2s_mutex, s2p_mutex);
  sector2page[sector] = bpage;
  page2sector[bpage] = sector;
}

void nvme_clear_mapping(buf_page_t *bpage) {
  if (!srv_use_nvme_hint) {
    return;
  }
  std::scoped_lock lock{p2s_mutex, s2p_mutex};
  uint64_t sector = nvme_get_sector_number(bpage);
  sector2page.erase(sector);
  page2sector.erase(bpage);
}

void nvme_send_host_hint(buf_page_t *bpage, uint32_t flag) {
  if (!srv_use_nvme_hint) {
    return;
  }
  if (bpage == nullptr || !buf_page_in_file(bpage)) {
    return;
  }

  int64_t sector = nvme_get_sector_number(bpage);
  if (sector == -1) {
    return;
  }

  int nvme_fd = open("/dev/nvme0n1", O_RDWR);
  if (nvme_fd < 0) {
    return;
  }

  const ulint page_size_phys = bpage->size.physical();
  struct nvme_dsm_range dsm_range;
  struct nvme_dsm_args args;
  const uint64_t sector_size = 512;
  uint32_t i;

  memset(&args, 0, sizeof(args));
  memset(&dsm_range, 0, sizeof(dsm_range));
  dsm_range.cattr = (flag);
  args.args_size = sizeof(args);
  args.fd = nvme_fd;
  args.timeout = 0;
  args.nsid = 1;
  args.nr_ranges = 1;
  args.dsm = &dsm_range;
  args.attrs = DSM_ATTR_BUFFER_HINT;
  args.result = (uint32_t *)malloc(sizeof(uint32_t));
  if (!args.result) {
    fprintf(stderr, "nvme_dsm failed allocating result buffer: %s\n",
            strerror(errno));
    close(nvme_fd);
    return;
  }

  dsm_range.nlb = (page_size_phys + sector_size - 1) / sector_size - 1;
  dsm_range.slba = sector;

  int rc = nvme_dsm(&args);
  if (rc < 0) {
    fprintf(stderr, "nvme_dsm failed: %s\n", strerror(errno));
  }

  free(args.result);
  close(nvme_fd);
}

void nvme_send_buffer_clean(buf_page_t *bpage) {
  nvme_send_host_hint(bpage, DSM_HINT_CLEAN);
}

void nvme_send_buffer_dirty(buf_page_t *bpage) {
  nvme_send_host_hint(bpage, DSM_HINT_DIRTY);
}

void nvme_send_buffer_evicted(buf_page_t *bpage) {
  nvme_send_host_hint(bpage, DSM_HINT_EVICTED);
}

std::shared_ptr<NVMe_Reclaim_Info> nvme_get_next_reclaim_info() {
  std::shared_ptr<NVMe_Reclaim_Info> info =
      std::make_shared<NVMe_Reclaim_Info>();

  return info;
}