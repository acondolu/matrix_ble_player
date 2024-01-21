#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Mapping.h>
#include <atomic>

#ifdef USE_SD
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#define filesystem SD
#endif
#ifdef USE_SPIFFS
#include "SPIFFS.h"
#define filesystem SPIFFS
#endif

extern VirtualMatrixPanel *virtualDisp;
extern void flip_matrix();

namespace {
  static AnimatedGIF gif;
  static TaskHandle_t task = NULL;
  static File current_gif_file;
  static uint32_t next_frame_millis = 0;
  static int spectre_gif_plz_stop = 1;
  static std::atomic<char*> next_gif_file(nullptr);

  // Draw a line of image directly on the LED Matrix
  void GIFDraw(GIFDRAW *pDraw) {
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth > V_MATRIX_WIDTH)
      iWidth = V_MATRIX_WIDTH;

    int off_x = (V_MATRIX_WIDTH  - gif.getCanvasWidth() )/2;
    int off_y = (V_MATRIX_HEIGHT - gif.getCanvasHeight())/2;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line

    if (pDraw->ucDisposalMethod == 2) { // restore to background color
      virtualDisp->fillRect(off_x, off_y + y, gif.getCanvasWidth(), 1, usPalette[pDraw->ucBackground]);
    }
    for (int x = 0; x < pDraw->iWidth; x++) {
      uint16_t color = pDraw->pPixels[x];
      // FIXME: we should also check something like if transparency is enabled
      if (color != pDraw->ucTransparent)
        virtualDisp->drawPixel(off_x + pDraw->iX + x, off_y + y, usPalette[color]); // 565 Color Format
    }
  }

  void *GIFOpenFile(const char *fname, int32_t *pSize) {
    current_gif_file = filesystem.open(fname);
    free((char *)fname);
    if (current_gif_file) {
      *pSize = current_gif_file.size();
      return (void *)&current_gif_file;
    }
    return NULL;
  }

  void GIFCloseFile(void *pHandle) {
    File *f = static_cast<File *>(pHandle);
    if (f != NULL)
      f->close();
  }

  int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
      iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
      return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
  }

  int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
    int i = micros();
    File *f = static_cast<File *>(pFile->fHandle);
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    i = micros() - i;
    return pFile->iPos;
  }

  void gifTask(void *parameter) {
    int t, i;
    for (;;) {
      vTaskDelay(1 / portTICK_PERIOD_MS);
      if (spectre_gif_plz_stop) continue;
      if (next_gif_file.load() != nullptr) {
        char* fp = next_gif_file.exchange(nullptr, std::memory_order_acq_rel);
        if (fp != nullptr) {
          if (current_gif_file) { // close old gif file
            current_gif_file.close();
          }
          // fp will be freed by GIFOpenFile
          if(gif.open(fp, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            spectre_gif_plz_stop = 0;
            next_frame_millis = 0;
            // clear both buffers
            virtualDisp->clearScreen();
            flip_matrix();
            virtualDisp->clearScreen();
          } else {
            // error
            spectre_gif_plz_stop = 1;
            continue;
          }
        }
      }
      // next frame
      t = millis();
      if (t < next_frame_millis) continue;
      int res = gif.playFrame(false, &i);
      if (res != 1) {
        int err = gif.getLastError();
        Serial.printf("gif.playFrame: %d %d\n", res, err);
        gif.reset();
      }
      next_frame_millis = t + i;
      flip_matrix();
      // copy front buffer into back buffer
      virtualDisp->copyDMABuffer();
    }
  }

}

namespace SpectreGif {

  void play(const char* filepath) {
    char* fp = strdup(filepath);
    char* old_fp = next_gif_file.exchange(fp, std::memory_order_acq_rel);
    if (old_fp != NULL) {
      free(old_fp);
    }
    spectre_gif_plz_stop = 0;
  }

  uint8_t init() {
    gif.begin(LITTLE_ENDIAN_PIXELS);
    return xTaskCreate(
        gifTask,   /* Task function. */
        "GifTask", /* String with name of task. */
        8192 * 2,        /* Stack size in bytes. */
        NULL,            /* Parameter passed as input of the task */
        5,               /* Priority of the task. */
        &task);           /* Task handle. */
  }

  void stop() {
    spectre_gif_plz_stop = 1;
  }

  bool isPlaying(const char* fp) {
    Serial.printf("Remove %s %s ?\n", current_gif_file.path(), fp);
    return current_gif_file && strcmp(current_gif_file.path(), fp) == 0;
  }

}