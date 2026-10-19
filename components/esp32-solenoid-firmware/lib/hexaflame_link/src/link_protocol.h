#pragma once

#include <stddef.h>
#include <stdint.h>

// HEXAFLAME host link - protocol side.
//
// A LinkProtocol turns bytes from the host into calls on an
// OutputController and writes its replies to a ByteSink. The transport
// (USB CDC, a hardware UART, RS-485, ...) is entirely the application's
// business: it pumps bytes in with feed() and ticks service() so the
// protocol can expire its own timeouts.
//
// Replacing the wire protocol means implementing this interface once and
// swapping the object that main.cpp constructs. Nothing else changes.

namespace hexaflame {

class ByteSink {
public:
  virtual ~ByteSink() = default;
  virtual void write(const uint8_t *data, size_t length) = 0;
};

struct LinkStats {
  uint32_t framesHandled = 0;   // well-formed frames addressed to this device
  uint32_t framesIgnored = 0;   // well-formed frames for another address
  uint32_t framesRejected = 0;  // bytes dropped while hunting for a valid frame
  uint32_t exceptionsSent = 0;  // requests answered with an error
  uint32_t lastFrameMs = 0;     // time of the last frame addressed to us
};

class LinkProtocol {
public:
  virtual ~LinkProtocol() = default;

  virtual const char *name() const = 0;

  // Inbound bytes from the transport, in order, any chunking.
  virtual void feed(const uint8_t *data, size_t length, uint32_t nowMs) = 0;

  // Called every main-loop iteration so the protocol can time out partial
  // frames and do any other housekeeping.
  virtual void service(uint32_t nowMs) = 0;

  virtual const LinkStats &stats() const = 0;
};

}  // namespace hexaflame
