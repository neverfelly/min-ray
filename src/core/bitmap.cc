

#include <ImfChannelList.h>
#include <ImfIO.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfStringAttribute.h>
#include <ImfVersion.h>
#include <min-ray/bitmap.h>

namespace min::ray {

Bitmap::Bitmap(const std::string &filename) {
  Imf::InputFile file(filename.c_str());
  const Imf::Header &header = file.header();
  const Imf::ChannelList &channels = header.channels();

  Imath::Box2i dw = file.header().dataWindow();
  resize(dw.max.y - dw.min.y + 1, dw.max.x - dw.min.x + 1);

  cout << "Reading a " << cols() << "x" << rows() << " OpenEXR file from \""
       << filename << "\"" << endl;

  const char *ch_r = nullptr, *ch_g = nullptr, *ch_b = nullptr;
  for (Imf::ChannelList::ConstIterator it = channels.begin(); it != channels.end(); ++it) {
    std::string name = toLower(it.name());

    if (it.channel().xSampling != 1 || it.channel().ySampling != 1) {
      /* Sub-sampled layers are not supported */
      continue;
    }

    if (!ch_r && (name == "r" || name == "red" ||
                  endsWith(name, ".r") || endsWith(name, ".red"))) {
      ch_r = it.name();
    } else if (!ch_g && (name == "g" || name == "green" ||
                         endsWith(name, ".g") || endsWith(name, ".green"))) {
      ch_g = it.name();
    } else if (!ch_b && (name == "b" || name == "blue" ||
                         endsWith(name, ".b") || endsWith(name, ".blue"))) {
      ch_b = it.name();
    }
  }

  if (!ch_r || !ch_g || !ch_b)
    throw NoriException("This is not a standard RGB OpenEXR file!");

  size_t compStride = sizeof(float),
         pixelStride = 3 * compStride,
         rowStride = pixelStride * cols();

  char *ptr = reinterpret_cast<char *>(data());

  Imf::FrameBuffer frameBuffer;
  frameBuffer.insert(ch_r, Imf::Slice(Imf::FLOAT, ptr, pixelStride, rowStride));
  ptr += compStride;
  frameBuffer.insert(ch_g, Imf::Slice(Imf::FLOAT, ptr, pixelStride, rowStride));
  ptr += compStride;
  frameBuffer.insert(ch_b, Imf::Slice(Imf::FLOAT, ptr, pixelStride, rowStride));
  file.setFrameBuffer(frameBuffer);
  file.readPixels(dw.min.y, dw.max.y);
}

void Bitmap::save(const std::string &filename) {
  cout << "Writing a " << cols() << "x" << rows()
       << " OpenEXR file to \"" << filename << "\"" << endl;

  Imf::Header header((int)cols(), (int)rows());
  header.insert("comments", Imf::StringAttribute("Generated by Nori"));

  Imf::ChannelList &channels = header.channels();
  channels.insert("R", Imf::Channel(Imf::FLOAT));
  channels.insert("G", Imf::Channel(Imf::FLOAT));
  channels.insert("B", Imf::Channel(Imf::FLOAT));

  Imf::FrameBuffer frameBuffer;
  size_t compStride = sizeof(float),
         pixelStride = 3 * compStride,
         rowStride = pixelStride * cols();

  char *ptr = reinterpret_cast<char *>(data());
  frameBuffer.insert("R", Imf::Slice(Imf::FLOAT, ptr, pixelStride, rowStride));
  ptr += compStride;
  frameBuffer.insert("G", Imf::Slice(Imf::FLOAT, ptr, pixelStride, rowStride));
  ptr += compStride;
  frameBuffer.insert("B", Imf::Slice(Imf::FLOAT, ptr, pixelStride, rowStride));

  Imf::OutputFile file(filename.c_str(), header);
  file.setFrameBuffer(frameBuffer);
  file.writePixels((int)rows());
}

}  // namespace min::ray