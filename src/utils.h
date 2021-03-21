// Spectral Compressor: an FFT based compressor
// Copyright (C) 2021 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <atomic>
#include <mutex>

#include <function2/function2.hpp>

/**
 * A wrapper around some resizeable type `T` that contains an active `T` and an
 * inactive `T`. When resizing, we'll resize the inactive `T`, and then set a
 * flag that will cause the active and the inactive objects to get swapped the
 * next time the audio thread requests a reference to the currently active
 * objects. This prevents locking and memory allocations on the audio thread.
 *
 * @tparam T The 'resizeable' type we should store. This has to be both move and
 *   copy constructable and assignable.
 */
template <typename T>
class AtomicResizable {
   public:
    /**
     * @param initial The initial value for the object. This will also be copied
     *   to the inactive slot.
     * @param resize_and_clear_fn This function should resize an object of type
     *   `T` and potentially also clear its values. While not strictly
     *   necessary, clearing may be a good idea to avoid weird pops and other
     *   artifacts.
     */
    AtomicResizable(T initial,
                    fu2::unique_function<void(T&, size_t)> resize_and_clear_fn)
        : resize_and_clear_fn(std::move(resize_and_clear_fn)),
          active(initial),
          inactive(initial) {}

    /**
     * Return a reference to currently active object. This should be done at the
     * start of the audio processing function, and the same reference should be
     * reused for the remainder of the function.
     */
    T& get() {
        // We'll swap these on the audio thread so that two resizes in a row in
        // between audio processing calls don't cause weird behaviour
        bool expected = true;
        if (needs_swap.compare_exchange_strong(expected, false)) {
            std::swap(active, inactive);
        }

        return active;
    }

    /**
     * Resize and clear the object. This may block and should never be called
     * from the audio thread.
     */
    void resize_and_clear(size_t new_size) {
        std::lock_guard lock(resize_mutex);

        // In case two resizes are performed in a row, we don't want the audio
        // thread swapping the objects while we're performing a second resize
        needs_swap = false;
        resize_and_clear_fn(inactive, new_size);
        needs_swap = true;
    }

   private:
    fu2::unique_function<void(T&, size_t)> resize_and_clear_fn;

    std::atomic_bool needs_swap = false;
    std::mutex resize_mutex;

    T active;
    T inactive;
};