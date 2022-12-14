#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <unistd.h>
#include <libgen.h>
#include <iostream>
#include <semaphore.h>

#include "utils.h"

using namespace std;
using namespace cv;

volatile int done = 0;

void* thread_capture_live_image(void* payload) {
  struct CamPayload* pl = (struct CamPayload*)payload;
  syslog(
    LOG_INFO, "thread%2d | thread_capture_live_image() started. device_uri: %s, shm_name: %s, sem_name: %s",
    pl->tid, pl->device_uri, pl->shm_name, pl->sem_name
  );
  
  Mat frame;
  bool result = false;
  VideoCapture cap;

  std::vector<uint8_t> encoded_img = {};
  std::vector<int> configs = {IMWRITE_JPEG_QUALITY, 80};
  const uint16_t interval = 30;
  uint32_t iter = interval;
  size_t img_size;

  int fd = shm_open(pl->shm_name, O_CREAT | O_RDWR, PERMS);
  if (fd < 0) {
    syslog(LOG_ERR, "thread%2d | shm_open(): %s. This thread will exit now.", pl->tid, strerror(errno));
    return NULL;
  }
  ftruncate(fd, SHM_SIZE);
  void* shmptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if ((void*) -1  == shmptr) {
    syslog(LOG_ERR, "thread%2d | mmap(): %s. This thread will exit now.", pl->tid, strerror(errno));
    close(fd);
    shm_unlink(pl->shm_name);
    return NULL;
  }
  syslog(LOG_INFO, "thread%2d | shared memory created, address: %p [0..%d]\n", pl->tid, shmptr, SHM_SIZE - 1);

  sem_t* semptr = sem_open(pl->sem_name, O_CREAT, PERMS, SEM_INITIAL_VALUE);
  if (semptr == (void*) -1) {
    syslog(LOG_ERR, "thread%2d | sem_open(): %s. This thread will exit now.", pl->tid, strerror(errno));
    munmap(shmptr, SHM_SIZE);  
    close(fd);
    shm_unlink(pl->shm_name);
    return NULL;
  }
  uint32_t sleep_sec = 5;
  while (!done) {
    
    if (cap.isOpened() == false || result == false) {
      syslog(LOG_INFO, "thread%2d | cap.open(%s)'ing...", pl->tid, pl->device_uri);
      cap.release();
      result = cap.open(pl->device_uri);
      //cap.set(CV_CAP_PROP_FOURCC, CV_FOURCC('H', '2', '6', '4'));
      if (cap.isOpened() && result) {
        syslog(LOG_INFO, "thread%2d | cap.open(%s)'ed", pl->tid, pl->device_uri);
      } else {
        syslog(LOG_ERR, "thread%2d | cap.open(%s) failed", pl->tid, pl->device_uri);
        result = false;
        // canNOT have continue here--we need to fill the screen with gray color
      }
    }    
    if (result) {
      result = cap.read(frame);
    }
    if (result == false) {
      syslog(
        LOG_ERR, "thread%2d | cap.read() failed, will cap.release() and then re-try after sleep(%d)", pl->tid, sleep_sec
      );
      cap.release();
      syslog(LOG_ERR, "thread%2d | cap.release()'ed", pl->tid);
      sleep(sleep_sec*=2);
      // The concern is that each retry attempt leaks some resources, so here we limit the retry frequency.
    } else {
      sleep_sec = 5;
    }
    ++iter;
    if (iter < interval) { continue; }

    if (result == false || frame.empty()) {
      frame = Mat(540, 960, CV_8UC3, Scalar(128, 128, 128));
    }
    iter = 0;
    imencode(".jpg", frame, encoded_img, configs);
    img_size = encoded_img.size();
    if (img_size > SHM_SIZE - 4) {
      syslog(
        LOG_ERR, "thread%2d | encoded_img.size() == %lu > SHM_SIZE == %d, skipped memcpy()...\n",
        pl->tid, img_size, SHM_SIZE
      );
      continue;
    }
    if (sem_wait(semptr) < 0) {
      syslog(LOG_ERR, "thread%2d | sem_wait(): %s. This thread will exit now.", pl->tid, strerror(errno));
      break;
    }
    memcpy(shmptr, &img_size, 4);
    memcpy((uint8_t*)shmptr + 4, &(encoded_img[0]), img_size);
    if (sem_post(semptr) < 0) {
      syslog(LOG_ERR, "thread%2d | sem_post(): %s. This thread will exit now.", pl->tid, strerror(errno));
      break;
    }
  }
  cap.release();
  sem_close(semptr);
  sem_unlink(pl->sem_name);
  munmap(shmptr, SHM_SIZE);  
  close(fd);
  shm_unlink(pl->shm_name);
  syslog(LOG_INFO, "thread%2d | thread_capture_live_image() exiting", pl->tid);
  return NULL;
}

static void signal_handler(int sig_num) {
  syslog(LOG_INFO, "main     | Signal %d received\n", sig_num);
  done = 1;  
}

void initialize_sig_handler() {
  struct sigaction act;
  act.sa_handler = signal_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_RESETHAND;
  sigaction(SIGINT, &act, 0);
  sigaction(SIGABRT, &act, 0);
  sigaction(SIGTERM, &act, 0);
}

int main(int argc, char *argv[]) {
  openlog("cam.odcs", LOG_PID | LOG_CONS, 0); // This is configured in /etc/rsyslog.d/
  
  initialize_sig_handler();
  initialize_paths(argv[0]);
  json_object* root = json_object_from_file(settings_path);
  json_object* root_app = json_object_object_get(root, "app");
  json_object* videos_devices = json_object_object_get(root_app, "video_devices"); 
  size_t videos_devices_len = json_object_array_length(videos_devices);
  //cout << getBuildInformation() << endl;
  syslog(LOG_INFO, "main     | getBuildInformation():\n%s", getBuildInformation().c_str());
  syslog(LOG_INFO, "main     | a total of %lu device(s) are defined in settings.json", videos_devices_len);
  struct CamPayload cpls[videos_devices_len];
  pthread_t tids[videos_devices_len];
  for (size_t i = 0; i < videos_devices_len; ++i){
    json_object* video_device = json_object_array_get_idx(videos_devices, i);
    cpls[i].device_uri = json_object_get_string(video_device);
    snprintf(cpls[i].sem_name, 32, "/odcs.sem%lu", i);
    snprintf(cpls[i].shm_name, 32, "/odcs.shm%lu", i);
    cpls[i].tid = i;
    if (pthread_create(&(tids[i]), NULL, thread_capture_live_image, &(cpls[i])) != 0) {
      done = 1;
      syslog(LOG_ERR, "Failed to create pthread_create(thread_capture_live_image), program will quit gracefully.");
    }
  }
  for (size_t i = 0; i < videos_devices_len; ++i) {
    pthread_join(tids[i], NULL);
    syslog(LOG_INFO, "main     | thread %lu exited gracefully", i);
  }
  json_object_put(root);
  free_paths();
  closelog();
  return 0;
}
