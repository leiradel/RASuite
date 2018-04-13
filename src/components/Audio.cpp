/*
Copyright (C) 2018 Andre Leiradella

This file is part of RALibretro.

RALibretro is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RALibretro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Audio.h"

#include <SDL_timer.h>
#include <string.h>

bool Fifo::init(size_t size)
{
  _mutex = SDL_CreateMutex();
  
  if (!_mutex)
  {
    return false;
  }
  
  _buffer = (uint8_t*)malloc(size);
  
  if (_buffer == NULL)
  {
    SDL_DestroyMutex(_mutex);
    return false;
  }
  
  _size = _avail = size;
  _first = _last = 0;
  return true;
}

void Fifo::destroy()
{
  ::free(_buffer);
  SDL_DestroyMutex(_mutex);
}

void Fifo::reset()
{
  _avail = _size;
  _first = _last = 0;
}

void Fifo::read(void* data, size_t size)
{
  SDL_LockMutex(_mutex);

  size_t first = size;
  size_t second = 0;
  
  if (first > _size - _first)
  {
    first = _size - _first;
    second = size - first;
  }
  
  uint8_t* src = _buffer + _first;
  memcpy(data, src, first);
  memcpy((uint8_t*)data + first, _buffer, second);
  
  _first = (_first + size) % _size;
  _avail += size;

  SDL_UnlockMutex(_mutex);
}

void Fifo::write(const void* data, size_t size)
{
  SDL_LockMutex(_mutex);

  size_t first = size;
  size_t second = 0;
  
  if (first > _size - _last)
  {
    first = _size - _last;
    second = size - first;
  }
  
  uint8_t* dest = _buffer + _last;
  memcpy(dest, data, first);
  memcpy(_buffer, (uint8_t*)data + first, second);
  
  _last = (_last + size) % _size;
  _avail -= size;

  SDL_UnlockMutex(_mutex);
}

size_t Fifo::occupied()
{
  size_t avail;

  SDL_LockMutex(_mutex);
  avail = _size - _avail;
  SDL_UnlockMutex(_mutex);

  return avail;
}

size_t Fifo::free()
{
  size_t avail;

  SDL_LockMutex(_mutex);
  avail = _avail;
  SDL_UnlockMutex(_mutex);

  return avail;
}

bool Audio::init(libretro::LoggerComponent* logger, double sample_rate, Fifo* fifo)
{
  _coreRate = 0;
  _resampler = NULL;

  _logger = logger;
  _sampleRate = sample_rate;

  _rateControlDelta = 0.005;
  _currentRatio = 0.0;
  _originalRatio = 0.0;

  _fifo = fifo;
  return true;
}

void Audio::destroy()
{
  if (_resampler != NULL)
  {
    speex_resampler_destroy(_resampler);
  }
}

bool Audio::setRate(double rate)
{
  if (_resampler != NULL)
  {
    speex_resampler_destroy(_resampler);
  }

  _coreRate = rate;
  _currentRatio = _originalRatio = _sampleRate / _coreRate;

  int error;
  _resampler = speex_resampler_init(2, _coreRate, _sampleRate, SPEEX_RESAMPLER_QUALITY_DEFAULT, &error);

  if (_resampler == NULL)
  {
    _logger->printf(RETRO_LOG_ERROR, "speex_resampler_init: %s", speex_resampler_strerror(error));
    return false;
  }
  else
  {
    _logger->printf(RETRO_LOG_INFO, "Resampler initialized to convert from %f to %f", _coreRate, _sampleRate);
  }

  return true;
}

void Audio::mix(const int16_t* samples, size_t frames)
{
  size_t avail = _fifo->free();

  /* Readjust the audio input rate. */
  int    half_size = (int)_fifo->size() / 2;
  int    delta_mid = (int)avail - half_size;
  double direction = (double)delta_mid / (double)half_size;
  double adjust    = 1.0 + _rateControlDelta * direction;

  _currentRatio = _originalRatio * adjust;

  spx_uint32_t in_len = frames * 2;
  spx_uint32_t out_len = (spx_uint32_t)(in_len * _currentRatio);
  out_len += out_len & 1;
  int16_t* output = (int16_t*)alloca(out_len * 2);

  if (output == NULL)
  {
    _logger->printf(RETRO_LOG_ERROR, "Error allocating output buffer");
    return;
  }

  int error = speex_resampler_process_int(_resampler, 0, samples, &in_len, output, &out_len);

  if (error != RESAMPLER_ERR_SUCCESS)
  {
    memset(output, 0, out_len * 2);
    _logger->printf(RETRO_LOG_ERROR, "speex_resampler_process_int: %s", speex_resampler_strerror(error));
  }

  size_t size = out_len * 2;
  
  while (size > avail)
  {
    SDL_Delay(1);
    avail = _fifo->free();
  }

  _fifo->write(output, size);
}
