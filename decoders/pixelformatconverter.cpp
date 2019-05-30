#include "pixelformatconverter.h"

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libswscale/swscale.h>
}

#include <QtMath>
#include <QDebug>

PixelFormatConverter* olive::pix_fmt_conv;

PixelFormatConverter::PixelFormatConverter()
{
  threads_.resize(qMax(1, QThread::idealThreadCount()-1));

  for (int i=0;i<threads_.size();i++) {
    threads_[i] = new PixFmtConvertThread();
    threads_[i]->start(QThread::HighPriority);
  }
}

PixelFormatConverter::~PixelFormatConverter()
{
  for (int i=0;i<threads_.size();i++) {
    delete threads_[i];
  }
}

void PixelFormatConverter::AVFrameToPipeline(uint8_t **input_buffer,
                                             int *input_linesize,
                                             const int& width,
                                             const int& height,
                                             AVPixelFormat input_fmt,
                                             void *output_buffer,
                                             olive::PixelFormat output_fmt)
{
  // TODO: Support all AVPixelFormats. There are many and this will be an ongoing process (though many will likely
  //       not need much effort (or any support at all). In the time-being this function will continue to use swscale
  //       (functioning as abstraction from the rest of the code) while remaining support is added.


  // Currently we don't natively support anything other than RGBA and RGBA64 so if it isn't this, we'll need to
  // swscale it to one of those for the timebeing
  uint8_t *converted_buffer = nullptr;
  int converted_linesize[AV_NUM_DATA_POINTERS];
  if (input_fmt != AV_PIX_FMT_RGBA && input_fmt != AV_PIX_FMT_RGBA64) {

    converted_buffer = new uint8_t[input_linesize[0] * height * 4];

    // Determine whether this is an 8-bit image or higher
    AVPixelFormat possible_pix_fmts[] = {
      AV_PIX_FMT_RGBA,
      AV_PIX_FMT_RGBA64,
      AV_PIX_FMT_NONE
    };

    AVPixelFormat pix_fmt = avcodec_find_best_pix_fmt_of_list(possible_pix_fmts,
                                                              input_fmt,
                                                              1,
                                                              nullptr);

    SwsContext* sws_ctx = sws_getContext(width,
                                         height,
                                         input_fmt,
                                         width,
                                         height,
                                         pix_fmt,
                                         0,
                                         nullptr,
                                         nullptr,
                                         nullptr);


    switch (pix_fmt) {
    case AV_PIX_FMT_RGBA:
      converted_linesize[0] = width * 4;
      break;
    case AV_PIX_FMT_RGBA64:
      converted_linesize[0] = width * 8;
      break;
    default:
      // We shouldn't really ever get here, but we may as well handle it
      qWarning() << "Invalid destination pixel format";
    }


    sws_scale(sws_ctx, input_buffer, input_linesize, 0, height, &converted_buffer, converted_linesize);

    sws_freeContext(sws_ctx);

    input_fmt = pix_fmt;
    input_buffer = &converted_buffer;

  }


  // Wait til other frame conversions are done
  mutex_.lock();


  // Split job between threads
  int lines_per_thread = qCeil(qreal(height)/qreal(threads_.size()));

  // Send conversion job to threads
  for (int i=0;i<threads_.size();i++) {

    int line_start = lines_per_thread*i;

    threads_.at(i)->Convert(input_buffer,
                            input_linesize,
                            width,
                            line_start,
                            qMin(lines_per_thread, height - line_start),
                            input_fmt,
                            output_buffer,
                            output_fmt);
  }

  // Wait for each thread to complete
  for (int i=0;i<threads_.size();i++) {
    threads_.at(i)->WaitUntilComplete();
  }


  mutex_.unlock();

  delete [] converted_buffer;
}

int PixelFormatConverter::GetBufferSize(olive::PixelFormat format, const int &width, const int &height)
{
  int rgba_channels = 4;

  switch (format) {
  case olive::PIX_FMT_RGBA8:
    return width * height * rgba_channels;
  case olive::PIX_FMT_RGBA16:
  case olive::PIX_FMT_RGBA16F:
    return 2 * width * height * rgba_channels;
  case olive::PIX_FMT_RGBA32F:
    return 4 * width * height * rgba_channels;
  default:
    return 0;
  }
}

PixFmtConvertThread::PixFmtConvertThread() :
  cancelled_(false)
{
}

void PixFmtConvertThread::run()
{
  mutex_.lock();

  while (!cancelled_) {
    wait_cond_.wait(&mutex_);
    if (cancelled_) break;

    Process();
  }

  mutex_.unlock();
}

void PixFmtConvertThread::Convert(uint8_t **input_buffer,
                                  int *input_linesize,
                                  const int &width,
                                  const int &line_start,
                                  const int &line_count,
                                  AVPixelFormat input_fmt,
                                  void *output_buffer,
                                  olive::PixelFormat output_fmt)
{
  mutex2_.lock();
  mutex_.lock();

  input_buffer_ = input_buffer;
  input_linesize_ = input_linesize;
  width_ = width;
  line_start_ = line_start;
  line_count_ = line_count;
  input_fmt_ = input_fmt;
  output_buffer_ = output_buffer;
  output_fmt_ = output_fmt;

  wait_cond_.wakeAll();

  mutex_.unlock();
}

void PixFmtConvertThread::WaitUntilComplete()
{
  mutex2_.lock();
  mutex_.lock();
  mutex2_.unlock();
  mutex_.unlock();
}

void PixFmtConvertThread::Cancel()
{
  cancelled_ = true;
  wait_cond_.wakeAll();
  wait();
}

void PixFmtConvertThread::Process()
{
  mutex2_.unlock();

  switch (input_fmt_) {
  case AV_PIX_FMT_RGBA:
  {
    int channels = 4; // FIXME RGBA magic number

    int byte_linesize = input_linesize_[0]*channels;

    int input_start = byte_linesize*line_start_;
    int output_start = width_*channels*line_start_;

    for (int i=0;i<line_count_;i++) {

      int input_line_start = input_start + byte_linesize*i;
      int input_line_end = width_*channels;

      int output_line_start = output_start + width_*channels*i;

      for (int j=0;j<input_line_end;j++) {

        // Convert 8-bit integer to float and store in output buffer
        static_cast<float*>(output_buffer_)[output_line_start+j] = input_buffer_[0][input_line_start+j] / 255.0f;

      }
    }
    break;
  }
  default:
    qWarning() << "PixFmtConvertThread doesn't yet know how to convert pixel format" << input_fmt_;
  }
}
