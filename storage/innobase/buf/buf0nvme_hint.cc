#include "buf0nvme_hint.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "buf0buf.h"
#include "fil0fil.h"
#include "scope_guard.h"
#include "srv0srv.h"
#include "ut0log.h"

std::mutex p2s_mutex;
std::mutex s2p_mutex;
std::unordered_map<buf_page_t *, uint64_t> page2sector;
std::unordered_map<uint64_t, buf_page_t *> sector2page;

namespace {
// For MPSC queue for NVMe hint requests
struct NvmeHintRequest {
  buf_page_t *bpage;
  uint32_t flag;
};

std::queue<NvmeHintRequest> hint_requests;
std::mutex hint_queue_mutex;
std::condition_variable hint_queue_cv;
std::vector<std::thread> nvme_hint_workers;
std::atomic<bool> shutdown_worker{false};

void nvme_hint_worker_func() {
  int nvme_fd = open("/dev/nvme0n1", O_RDWR);
  if (nvme_fd < 0) {
    return;
  }

  Scope_guard close_guard = create_scope_guard([&] { close(nvme_fd); });

  while (true) {
    NvmeHintRequest request;
    {
      std::unique_lock<std::mutex> lock(hint_queue_mutex);
      hint_queue_cv.wait(
          lock, [] { return !hint_requests.empty() || shutdown_worker; });

      if (shutdown_worker && hint_requests.empty()) {
        return;
      }

      request = hint_requests.front();
      hint_requests.pop();
    }
    int64_t sector = nvme_get_sector_number(request.bpage);
    if (sector == -1) {
      return;
    }

    const uint64_t page_size_phys = 4096;
    const uint64_t sector_size = 512;
    struct nvme_dsm_range dsm_range;
    struct nvme_dsm_args args;

    memset(&args, 0, sizeof(args));
    memset(&dsm_range, 0, sizeof(dsm_range));
    dsm_range.cattr = (request.flag);
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
      continue;
    }

    dsm_range.nlb = (page_size_phys + sector_size - 1) / sector_size - 1;
    dsm_range.slba = sector;

    int rc = nvme_dsm(&args);
    if (rc < 0) {
      fprintf(stderr, "nvme_dsm failed: %s\n", strerror(errno));
    }

    free(args.result);
  }
}
}  // namespace

#define NUM_HINT_WORKERS 8

void nvme_map_init() {
  std::scoped_lock lock{p2s_mutex, s2p_mutex};
  page2sector.clear();
  sector2page.clear();

  shutdown_worker = false;
  for (int i = 0; i < NUM_HINT_WORKERS; ++i) {
    nvme_hint_workers.emplace_back(nvme_hint_worker_func);
  }
}

void nvme_map_free() {
  shutdown_worker = true;
  hint_queue_cv.notify_all();
  for (auto &nvme_hint_worker : nvme_hint_workers) {
    if (nvme_hint_worker.joinable()) {
      nvme_hint_worker.join();
    }
  }

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
  {
    std::lock_guard lock(p2s_mutex);
    if (page2sector.contains(bpage)) {
      return page2sector[bpage];
    }
  }
  return nvme_set_mapping(bpage);
}

buf_page_t *nvme_get_bpage(uint64_t sector) {
  std::lock_guard lock(s2p_mutex);
  if (!sector2page.contains(sector)) {
    return nullptr;
  }
  return sector2page[sector];
}

int64_t nvme_set_mapping(buf_page_t *bpage) {
  if (bpage == nullptr || !buf_page_in_file(bpage)) {
    return -1;
  }
  fil_space_t *space = bpage->get_space();

  if (space == nullptr) {
    return -1;
  }

  auto page_no = bpage->page_no();
  fil_node_t *fil_node = space->get_file_node(&page_no);
  const char *path = fil_node->name;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }

  const ulint page_size_phys = bpage->size.physical();
  const off_t offset = (off_t)bpage->id.page_no() * page_size_phys;

  struct fiemap *map = get_fiemap(fd, offset, page_size_phys);
  close(fd);
  if (map == NULL) {
    return -1;
  }
  Scope_guard free_guard = create_scope_guard([&] { free(map); });

  if (map->fm_mapped_extents <= 0) {
    return -1;
  }

  uint64_t p_offset = map->fm_extents[0].fe_physical;
  uint64_t sector_size = 512;
  uint64_t sector = p_offset / sector_size;

  std::scoped_lock lock(p2s_mutex, s2p_mutex);
  sector2page[sector] = bpage;
  page2sector[bpage] = sector;
  return sector;
}

void nvme_clear_mapping(buf_page_t *bpage) {
  uint64_t sector = nvme_get_sector_number(bpage);
  std::scoped_lock lock{p2s_mutex, s2p_mutex};
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

  {
    std::lock_guard<std::mutex> lock(hint_queue_mutex);
    hint_requests.push({bpage, flag});
  }
  hint_queue_cv.notify_one();
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