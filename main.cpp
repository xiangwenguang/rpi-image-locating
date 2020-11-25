#include "http.h"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <pthread.h>

char *itoa(unsigned x, char *s) {
  do
    *s++ = x % 10, x /= 10;
  while (x);
  return s;
}

const int PYR_CNT = 3, VIDEO_W = 234, VIDEO_H = 156;

cv::VideoCapture video;
cv::Mat video_pic, orig_pic;
short stat[3]; // [0] = x, [1] = y, [2] = tick
pthread_mutex_t mu;

[[noreturn]] void *video_server_fn(void *) {
  serve(8080, [](int fd) {
    write(fd, "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=--jpg\r\n\r\n", 76);
    std::vector<unsigned char> data;
    int cnt = 0;
    while (true) {
      pthread_mutex_lock(&mu);
      cv::imencode(".jpg", video_pic, data);
      pthread_mutex_unlock(&mu);
      static char buf[128] = "--jpgHTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-length: ";
      char *s = buf + 64;
      s = itoa(data.size(), s);
      mempcpy(s, "\r\n\r\n", 4);
      write(fd, buf, s + 4 - buf);
      if (write(fd, data.data(), data.size()) == -1) break;
    }
  });
}

[[noreturn]] void *pos_server_fn(void *) {
  serve(8081, [](int fd) {
    char rd[16];
    read(fd, rd, sizeof(rd));
    if (rd[10] == '=') { // POST /?pic=x
      int id = rd[11] - '0';
      stat[0] = id, stat[1] = stat[2] = 0; // for debug
    }
    static char buf[] = "HTTP/1.1 200 OK\r\nContent-Type: application/x-binary\r\nAccess-Control-Allow-Origin: null\r\nContent-length: 6\r\n\r\n\0\0\0\0\0\0\0\0\0";
    memcpy(buf + sizeof(buf) - sizeof(stat), stat, sizeof(stat));
    write(fd, buf, sizeof(buf));
  });
}

[[noreturn]] void *calc_fn(void *) {
  cv::Mat temp, result;
  while (true) {
    long long beg = cv::getTickCount();
    pthread_mutex_lock(&mu);
    cv::cvtColor(video_pic, temp, cv::COLOR_BGR2GRAY);
    pthread_mutex_unlock(&mu);
    long long cur1 = cv::getTickCount(), el1 = (cur1 - beg) / 1000000;
    beg = cur1;
    for (int i = 0; i < PYR_CNT; ++i) cv::pyrDown(temp, temp);
    cv::matchTemplate(orig_pic, temp, result, cv::TM_CCORR_NORMED);
    cv::Point max_loc;
    cv::minMaxLoc(result, nullptr, nullptr, nullptr, &max_loc);
    stat[0] = max_loc.x, stat[1] = max_loc.y;
    if (stat[2]++ == 30) stat[2] = 0;
    long long cur2 = cv::getTickCount(), el2 = (cur2 - beg) / 1000000;
    stat[3] = el1;
    stat[4] = el2;
  }
}

int main() {
  {
    const int W = 1920 / (1 << PYR_CNT), H = 1080 / (1 << PYR_CNT);
    extern unsigned char data[W * H];
    static int size[2] = {H, W};
    static size_t step[2] = {W, 1};
    orig_pic.flags = 0x42ff4000, orig_pic.dims = 2, orig_pic.rows = H, orig_pic.cols = W;
    orig_pic.datastart = orig_pic.data = data, orig_pic.dataend = orig_pic.datalimit = data + W * H;
    orig_pic.size.p = size, orig_pic.step.p = step;
  }
  video.open(0);
  video.read(video_pic); // guarantee video_pic not empty
  cv::resize(video_pic, video_pic, cv::Size(VIDEO_W, VIDEO_H));
  pthread_t th;
  pthread_create(&th, nullptr, video_server_fn, nullptr), pthread_detach(th);
  pthread_create(&th, nullptr, pos_server_fn, nullptr), pthread_detach(th);
  pthread_create(&th, nullptr, calc_fn, nullptr), pthread_detach(th);
  while (true) {
    if (video.grab()) {
      pthread_mutex_lock(&mu);
      video.retrieve(video_pic);
      cv::resize(video_pic, video_pic, cv::Size(VIDEO_W, VIDEO_H));
      pthread_mutex_unlock(&mu);
    }
  }
}