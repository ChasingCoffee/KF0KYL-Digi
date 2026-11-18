#include "SstvCodec.hpp"

#include <QtMath>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <functional>

namespace sstv
{
  namespace
  {
    constexpr double fs = 12000.;          // receive sample rate
    constexpr double centre = 1750.;       // demodulator centre
    constexpr double black = 1500.;
    constexpr double white = 2300.;
    constexpr double sync_hz = 1200.;
    constexpr int sep_robot36 = -2;        // Robot 36 separator: 1500 Hz on even lines, 2300 Hz on odd

    Segment tone (double ms, double hz) {return Segment {Segment::Tone, ms, hz, -1};}
    Segment chan (double ms, int c) {return Segment {Segment::Channel, ms, 0., c};}

    Mode martin (QString const& name, int vis, double scan)
    {
      return Mode {name, vis, 320, 256, 256, Colour::GBR, 0.,
          {tone (4.862, sync_hz), tone (0.572, black), chan (scan, 0), tone (0.572, black),
           chan (scan, 1), tone (0.572, black), chan (scan, 2), tone (0.572, black)}};
    }

    Mode scottie (QString const& name, int vis, double scan)
    {
      return Mode {name, vis, 320, 256, 256, Colour::GBR, 9.0,
          {tone (1.5, black), chan (scan, 0), tone (1.5, black), chan (scan, 1),
           tone (9.0, sync_hz), tone (1.5, black), chan (scan, 2)}};
    }

    Mode pd (QString const& name, int vis, int width, int height, double pixel_ms)
    {
      double const scan = width * pixel_ms;
      return Mode {name, vis, width, height, height / 2, Colour::PD, 0.,
          {tone (20.0, sync_hz), tone (2.08, black), chan (scan, 0), chan (scan, 1), chan (scan, 2), chan (scan, 3)}};
    }

    int channel_count (Colour c)
    {
      switch (c)
        {
        case Colour::Robot36: return 2;
        case Colour::PD: return 4;
        default: return 3;
        }
    }

    QRgb yuv (int y, int cb, int cr)
    {
      double const yy = 298.082 * (y - 16);
      int const r = qRound (0.003906 * (yy + 408.583 * (cr - 128)));
      int const g = qRound (0.003906 * (yy - 100.291 * (cb - 128) - 208.12 * (cr - 128)));
      int const b = qRound (0.003906 * (yy + 516.411 * (cb - 128)));
      return qRgb (qBound (0, r, 255), qBound (0, g, 255), qBound (0, b, 255));
    }

    void to_yuv (QRgb c, int& y, int& cb, int& cr)
    {
      double const r = qRed (c), g = qGreen (c), b = qBlue (c);
      y = qBound (0, qRound (16. + (65.738 * r + 129.057 * g + 25.064 * b) / 256.), 255);
      cb = qBound (0, qRound (128. + (-37.945 * r - 74.494 * g + 112.439 * b) / 256.), 255);
      cr = qBound (0, qRound (128. + (112.439 * r - 94.154 * g - 18.285 * b) / 256.), 255);
    }

    double hz_for (int value) {return black + value * (white - black) / 255.;}
  }

  double Mode::line_ms () const
  {
    double t = 0.;
    for (auto const& s : segments) t += s.ms;
    return t;
  }

  double Mode::sync_offset_ms () const
  {
    double t = 0.;
    for (auto const& s : segments)
      {
        if (Segment::Tone == s.kind && sync_hz == s.hz) return t;
        t += s.ms;
      }
    return 0.;
  }

  double Mode::sync_ms () const
  {
    for (auto const& s : segments)
      {
        if (Segment::Tone == s.kind && sync_hz == s.hz) return s.ms;
      }
    return 5.;
  }

  double Mode::seconds () const
  {
    return (pre_sync_ms + tx_lines * line_ms ()) / 1000.;
  }

  QVector<Mode> const& modes ()
  {
    static QVector<Mode> const list {
      martin ("Martin M1", 44, 146.432),
      martin ("Martin M2", 40, 73.216),
      scottie ("Scottie S1", 60, 138.240),
      scottie ("Scottie S2", 56, 88.064),
      scottie ("Scottie DX", 76, 345.6),
      Mode {"Robot 36", 8, 320, 240, 240, Colour::Robot36, 0.,
            {tone (9.0, sync_hz), tone (3.0, black), chan (88.0, 0),
             Segment {Segment::Tone, 4.5, sep_robot36, -1}, tone (1.5, 1900.), chan (44.0, 1)}},
      Mode {"Robot 72", 12, 320, 240, 240, Colour::Robot72, 0.,
            {tone (9.0, sync_hz), tone (3.0, black), chan (138.0, 0),
             tone (4.5, black), tone (1.5, 1900.), chan (69.0, 1),
             tone (4.5, white), tone (1.5, black), chan (69.0, 2)}},
      pd ("PD50", 93, 320, 256, 0.286),
      pd ("PD90", 99, 320, 256, 0.532),
      pd ("PD120", 95, 640, 496, 0.19),
      pd ("PD160", 98, 512, 400, 0.382),
      pd ("PD180", 96, 640, 496, 0.286),
      pd ("PD240", 97, 640, 496, 0.382),
      pd ("PD290", 94, 800, 616, 0.286),
    };
    return list;
  }

  Mode const * mode_by_vis (int vis)
  {
    for (auto const& m : modes ()) if (m.vis == vis) return &m;
    return nullptr;
  }

  Mode const * mode_by_name (QString const& name)
  {
    for (auto const& m : modes ()) if (m.name == name) return &m;
    return nullptr;
  }

  // ------------------------------------------------------------------
  // transmit

  QVector<float> encode (QImage const& source, Mode const& mode, QString const& call, double rate)
  {
    QImage const pic = source.convertToFormat (QImage::Format_RGB32)
      .scaled (mode.width, mode.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QVector<float> out;
    out.reserve (int ((mode.seconds () + 2.) * rate));
    double phase = 0., due = 0.;
    auto tone_out = [&] (double hz, double ms) {
      due += ms * rate / 1000.;
      double const step = 2. * M_PI * hz / rate;
      while (out.size () < due)
        {
          phase += step;
          if (phase > 2. * M_PI) phase -= 2. * M_PI;
          out << float (0.95 * std::sin (phase));
        }
    };

    // a short gap, then the calibration header and VIS code
    tone_out (black, 100.);
    tone_out (1900., 300.);
    tone_out (sync_hz, 10.);
    tone_out (1900., 300.);
    tone_out (sync_hz, 30.);
    int ones = 0;
    for (int i = 0; i < 7; ++i)
      {
        bool const bit = (mode.vis >> i) & 1;
        ones += bit;
        tone_out (bit ? 1100. : 1300., 30.);
      }
    tone_out ((ones & 1) ? 1100. : 1300., 30.);    // even parity
    tone_out (sync_hz, 30.);
    if (mode.pre_sync_ms > 0.) tone_out (sync_hz, mode.pre_sync_ms);

    int const w = mode.width;
    QVector<int> ys (w), cbs (w), crs (w), ys2 (w), cbs2 (w), crs2 (w);
    auto row_yuv = [&] (int row, QVector<int>& y, QVector<int>& cb, QVector<int>& cr) {
      auto const * line = reinterpret_cast<QRgb const *> (pic.constScanLine (qBound (0, row, mode.height - 1)));
      for (int x = 0; x < w; ++x) to_yuv (line[x], y[x], cb[x], cr[x]);
    };

    for (int l = 0; l < mode.tx_lines; ++l)
      {
        QVector<QVector<int>> ch (channel_count (mode.colour), QVector<int> (w));
        switch (mode.colour)
          {
          case Colour::GBR:
            {
              auto const * line = reinterpret_cast<QRgb const *> (pic.constScanLine (l));
              for (int x = 0; x < w; ++x)
                {
                  ch[0][x] = qGreen (line[x]);
                  ch[1][x] = qBlue (line[x]);
                  ch[2][x] = qRed (line[x]);
                }
              break;
            }
          case Colour::Robot36:
            {
              // even lines carry R-Y, odd lines B-Y, each the average of the pair
              int const first = l & ~1;
              row_yuv (first, ys, cbs, crs);
              row_yuv (first + 1, ys2, cbs2, crs2);
              QVector<int> y (w);
              row_yuv (l, y, cbs, crs);
              row_yuv (first, ys, cbs, crs);
              for (int x = 0; x < w; ++x)
                {
                  ch[0][x] = y[x];
                  ch[1][x] = (l & 1) ? (cbs[x] + cbs2[x]) / 2 : (crs[x] + crs2[x]) / 2;
                }
              break;
            }
          case Colour::Robot72:
            row_yuv (l, ys, cbs, crs);
            for (int x = 0; x < w; ++x) {ch[0][x] = ys[x]; ch[1][x] = crs[x]; ch[2][x] = cbs[x];}
            break;
          case Colour::PD:
            row_yuv (2 * l, ys, cbs, crs);
            row_yuv (2 * l + 1, ys2, cbs2, crs2);
            for (int x = 0; x < w; ++x)
              {
                ch[0][x] = ys[x];
                ch[1][x] = (crs[x] + crs2[x]) / 2;
                ch[2][x] = (cbs[x] + cbs2[x]) / 2;
                ch[3][x] = ys2[x];
              }
            break;
          }

        for (auto const& s : mode.segments)
          {
            if (Segment::Tone == s.kind)
              {
                double hz = s.hz;
                if (sep_robot36 == hz) hz = (l & 1) ? white : black;
                tone_out (hz, s.ms);
              }
            else
              {
                double const pixel = s.ms / w;
                for (int x = 0; x < w; ++x) tone_out (hz_for (ch[s.channel][x]), pixel);
              }
          }
      }

    // FSK ID: 0x20 0x2A, the call (each character less 0x20), 0x01 and an
    // XOR check; six bits each, least significant first, 22 ms a bit,
    // 1900 Hz for a one and 2100 Hz for a zero
    auto const id = call.trimmed ().toUpper ().replace (QChar {0x00D8}, '0');
    if (!id.isEmpty ())
      {
        QVector<int> symbols {0x20, 0x2A};
        int check = 0;
        for (auto c : id.left (10))
          {
            int const v = c.unicode () - 0x20;
            if (v < 0 || v > 63) continue;
            symbols << v;
            check ^= v;
          }
        symbols << 0x01 << check;
        tone_out (black, 100.);
        for (int v : symbols)
          {
            for (int b = 0; b < 6; ++b) tone_out (((v >> b) & 1) ? 1900. : 2100., 22.);
          }
      }
    tone_out (black, 50.);
    return out;
  }

  // ------------------------------------------------------------------
  // receive

  Decoder::Decoder (QObject * parent)
    : QObject {parent}
  {
    // low-pass for the mixed-down signal: about 1 kHz either side of the centre
    int const taps = 33;
    double const cutoff = 1000. / fs;
    fir_.resize (taps);
    double sum = 0.;
    for (int i = 0; i < taps; ++i)
      {
        double const n = i - (taps - 1) / 2.;
        double const sinc = (0. == n) ? 2. * cutoff : std::sin (2. * M_PI * cutoff * n) / (M_PI * n);
        double const window = 0.54 - 0.46 * std::cos (2. * M_PI * i / (taps - 1));
        fir_[i] = float (sinc * window);
        sum += fir_[i];
      }
    for (auto& t : fir_) t = float (t / sum);
    hist_.assign (taps, {0.f, 0.f});
    low_.push_back (0);
  }

  void Decoder::add_samples (short const * samples, int count)
  {
    int const taps = int (fir_.size ());
    double const step = 2. * M_PI * centre / fs;
    for (int i = 0; i < count; ++i)
      {
        float const x = samples[i] / 32768.f;
        osc_ += step;
        if (osc_ > 2. * M_PI) osc_ -= 2. * M_PI;
        hist_[hist_pos_] = std::complex<float> (x * float (std::cos (osc_)), -x * float (std::sin (osc_)));
        std::complex<float> y {0.f, 0.f};
        int k = hist_pos_;
        for (int t = 0; t < taps; ++t)
          {
            y += fir_[t] * hist_[k];
            if (--k < 0) k = taps - 1;
          }
        if (++hist_pos_ == taps) hist_pos_ = 0;
        auto const d = y * std::conj (last_);
        last_ = y;
        // the phase steps of the last millisecond, summed, give a steady
        // frequency even in noise (stronger samples count for more)
        disc_.push_back (d);
        run_sum_ += d;
        if (disc_.size () > 12) run_sum_ -= disc_[disc_.size () - 13];
        float hz = float (std::atan2 (run_sum_.imag (), run_sum_.real ()) * fs / (2. * M_PI) + centre);
        freq_.push_back (hz);
        low_.push_back (low_.back () + (hz < 1350.f ? 1 : 0));
        ++total_;
        ms_acc_ += d;
        if (++ms_count_ == 12)
          {
            process_ms (std::atan2 (ms_acc_.imag (), ms_acc_.real ()) * fs / (2. * M_PI) + centre);
            ms_acc_ = {0.f, 0.f};
            ms_count_ = 0;
          }
      }
    if (Picture == state_) decode_lines (false);
    if (FskId == state_ && total_ - fsk_from_ > qint64 (2.6 * fs))
      {
        look_for_fsk_id ();
        state_ = Listening;
      }
    if (Listening == state_) trim ();
  }

  void Decoder::trim ()
  {
    if (freq_.size () > size_t (6 * fs))
      {
        size_t const cut = freq_.size () - size_t (3 * fs);
        freq_.erase (freq_.begin (), freq_.begin () + cut);
        disc_.erase (disc_.begin (), disc_.begin () + cut);
        int const drop = low_[cut];
        low_.erase (low_.begin (), low_.begin () + cut);
        for (auto& v : low_) v -= drop;
        base_ += qint64 (cut);
      }
    if (ms_.size () > 8000)
      {
        size_t const cut = ms_.size () - 4000;
        ms_.erase (ms_.begin (), ms_.begin () + cut);
        ms_base_ += qint64 (cut);
      }
  }

  float Decoder::freq_at (qint64 sample) const
  {
    qint64 const i = sample - base_;
    if (i < 0 || i >= qint64 (freq_.size ())) return float (black);
    return freq_[size_t (i)];
  }

  double Decoder::mean_freq (double from, double to) const
  {
    qint64 a = qint64 (std::floor (from + 0.5)), b = qint64 (std::floor (to + 0.5));
    if (b <= a) b = a + 1;
    std::complex<float> sum {0.f, 0.f};
    for (qint64 k = a; k < b; ++k)
      {
        qint64 const i = k - base_;
        if (i >= 0 && i < qint64 (disc_.size ())) sum += disc_[size_t (i)];
      }
    if (0.f == sum.real () && 0.f == sum.imag ()) return black;
    return std::atan2 (sum.imag (), sum.real ()) * fs / (2. * M_PI) + centre;
  }

  int Decoder::value_at (double from, double to) const
  {
    double const hz = mean_freq (from, to);
    return qBound (0, qRound ((hz - black) * 255. / (white - black)), 255);
  }

  void Decoder::process_ms (double hz)
  {
    ms_.push_back (float (hz));
    if (Listening != state_ || !auto_start_) return;
    // Look back 305 ms for a VIS code: a 1900 Hz leader, a 30 ms start bit
    // at 1200 Hz, seven data bits and a parity bit (1100 Hz is a one,
    // 1300 Hz a zero) and a 1200 Hz stop bit. Every millisecond is a
    // possible start; the best fit nearby is taken.
    qint64 const n = qint64 (ms_.size ());
    if (n < 700) return;
    auto mean = [&] (qint64 from, qint64 to) {     // ms indices relative to ms_
      double sum = 0.;
      for (qint64 k = from; k < to; ++k) sum += ms_[size_t (k)];
      return sum / double (to - from);
    };
    auto check = [&] (qint64 s0, int& vis, double& err) -> bool {
      // s0: first ms of the start bit
      double const leader = mean (s0 - 260, s0 - 20);
      if (std::fabs (leader - 1900.) > 45.) return false;
      int in_leader = 0;
      for (qint64 k = s0 - 260; k < s0 - 20; ++k) in_leader += std::fabs (ms_[size_t (k)] - 1900.) < 120.;
      if (in_leader < 150) return false;
      double const start = mean (s0 + 4, s0 + 26), stop = mean (s0 + 274, s0 + 296);
      if (std::fabs (start - sync_hz) > 50. || std::fabs (stop - sync_hz) > 50.) return false;
      err = std::fabs (start - sync_hz) + std::fabs (stop - sync_hz);
      int bits = 0, ones = 0;
      for (int i = 0; i < 8; ++i)
        {
          double const f = mean (s0 + 30 + 30 * i + 5, s0 + 30 + 30 * i + 25);
          bool const one = f < sync_hz;
          double const off = std::fabs (f - (one ? 1100. : 1300.));
          if (off > 60.) return false;
          err += off;
          if (one) {bits |= 1 << i; ++ones;}
        }
      if (ones & 1) return false;
      vis = bits & 0x7f;
      return nullptr != mode_by_vis (vis);
    };
    qint64 const s0 = n - 306;
    int vis;
    double err;
    if (s0 - 262 < 0 || !check (s0, vis, err)) return;
    // the best start within a few ms
    qint64 best = s0;
    double best_err = err;
    for (qint64 c = s0 + 1; c <= s0 + 4 && c + 300 <= n; ++c)
      {
        int v;
        double e;
        if (check (c, v, e) && v == vis && e < best_err) {best = c; best_err = e;}
      }
    if (best + 304 > n) return;   // wait until the better start can be checked
    start_picture (*mode_by_vis (vis), (ms_base_ + best + 300) * 12);
  }

  void Decoder::start_picture (Mode const& mode, qint64 start_sample)
  {
    state_ = Picture;
    mode_ = &mode;
    samples_per_line_ = mode.line_ms () * fs / 1000.;
    origin_ = start_sample + mode.pre_sync_ms * fs / 1000.;
    fit_a_ = origin_;
    fit_b_ = samples_per_line_;
    sync_line_.clear ();
    sync_pos_.clear ();
    lines_decoded_ = 0;
    missed_ = 0;
    found_recent_ = 0;
    channels_ = QVector<QVector<QVector<quint8>>> (mode.tx_lines,
        QVector<QVector<quint8>> (channel_count (mode.colour), QVector<quint8> (mode.width, 0)));
    image_ = QImage {mode.width, mode.height, QImage::Format_RGB32};
    image_.fill (Qt::black);
    rows_done_ = 0;
    Q_EMIT started (mode.name);
  }

  double Decoder::line_start (int line) const
  {
    if (sync_line_.isEmpty ()) return origin_ + line * samples_per_line_;
    return fit_a_ + fit_b_ * line;
  }

  void Decoder::decode_lines (bool final_pass)
  {
    if (!mode_) return;
    double const sync_off = mode_->sync_offset_ms () * fs / 1000.;
    int const sync_len = qMax (6, int (mode_->sync_ms () * fs / 1000.));
    bool changed = false;
    while (lines_decoded_ < mode_->tx_lines)
      {
        int const l = lines_decoded_;
        double const predicted = line_start (l);
        if (!final_pass && total_ < qint64 (predicted + samples_per_line_ + 0.008 * fs)) break;

        // find this line's sync pulse near where it should be
        double const expect = predicted + sync_off;
        int const window = int ((l < 4 ? 0.010 : 0.004) * fs);
        double best = 0.;
        qint64 best_at = 0;
        // the pulse's trailing edge is the reliable mark: the latest
        // position that scores (nearly) as well as the best one, so a
        // 1200 Hz tone just before the sync (the VIS stop bit) can't pull
        // it early
        QVector<double> scores;
        qint64 const from = qint64 (expect) - window;
        for (qint64 p = from; p <= qint64 (expect) + window; ++p)
          {
            qint64 const a = p - base_, b = a + sync_len;
            double score = 0.;
            if (a >= 0 && b < qint64 (low_.size ())) score = double (low_[size_t (b)] - low_[size_t (a)]) / sync_len;
            scores << score;
            best = qMax (best, score);
          }
        for (int i = scores.size () - 1; i >= 0; --i)
          {
            if (scores[i] >= best - 1e-6) {best_at = from + i; break;}
          }
        if (best >= 0.55)
          {
            sync_line_ << l;
            sync_pos_ << (best_at - 6 - sync_off);
            missed_ = 0;
            found_recent_ = qMin (20, found_recent_ + 1);
            // fit start = a + b * line to the syncs found so far
            int const n = sync_line_.size ();
            if (slant_fix_ && n >= 6)
              {
                for (int pass = 0; pass < 2; ++pass)
                  {
                    double sx = 0, sy = 0, sxx = 0, sxy = 0;
                    int used = 0;
                    for (int i = 0; i < n; ++i)
                      {
                        double const x = sync_line_[i], y = sync_pos_[i];
                        if (pass && std::fabs (y - (fit_a_ + fit_b_ * x)) > 0.0015 * fs) continue;
                        sx += x; sy += y; sxx += x * x; sxy += x * y; ++used;
                      }
                    double const den = used * sxx - sx * sx;
                    if (used >= 4 && std::fabs (den) > 1e-9)
                      {
                        fit_b_ = (used * sxy - sx * sy) / den;
                        fit_a_ = (sy - fit_b_ * sx) / used;
                      }
                  }
                // a clock can't be off by much: keep the line length sensible
                fit_b_ = qBound (samples_per_line_ * 0.99, fit_b_, samples_per_line_ * 1.01);
              }
            else
              {
                QVector<double> offsets;
                for (int i = 0; i < n; ++i) offsets << sync_pos_[i] - samples_per_line_ * sync_line_[i];
                std::sort (offsets.begin (), offsets.end ());
                fit_a_ = offsets[n / 2];
                fit_b_ = samples_per_line_;
              }
          }
        else
          {
            ++missed_;
            found_recent_ = qMax (0, found_recent_ - 1);
          }
        render_line (l, line_start (l));
        ++lines_decoded_;
        changed = true;
        if (missed_ >= 25)
          {
            finish (false);
            return;
          }
      }
    if (changed) Q_EMIT progress (rows_done_, mode_->height);
    if (lines_decoded_ >= mode_->tx_lines) finish (true);
  }

  void Decoder::render_line (int l, double start)
  {
    double t = start;
    auto& chans = channels_[l];
    for (auto const& s : mode_->segments)
      {
        double const len = s.ms * fs / 1000.;
        if (Segment::Channel == s.kind)
          {
            int const w = mode_->width;
            double const px = len / w;
            auto& c = chans[s.channel];
            for (int x = 0; x < w; ++x) c[x] = quint8 (value_at (t + x * px, t + (x + 1) * px));
          }
        t += len;
      }

    int const w = mode_->width;
    auto put_row = [&] (int row, std::function<QRgb (int)> const& pixel) {
      if (row < 0 || row >= image_.height ()) return;
      auto * line = reinterpret_cast<QRgb *> (image_.scanLine (row));
      for (int x = 0; x < w; ++x) line[x] = pixel (x);
      rows_done_ = qMax (rows_done_, row + 1);
    };
    switch (mode_->colour)
      {
      case Colour::GBR:
        put_row (l, [&] (int x) {return qRgb (chans[2][x], chans[0][x], chans[1][x]);});
        break;
      case Colour::Robot36:
        if (l & 1)
          {
            auto const& even = channels_[l - 1];
            for (int r : {l - 1, l})
              {
                auto const& y = channels_[r][0];
                put_row (r, [&] (int x) {return yuv (y[x], chans[1][x], even[1][x]);});
              }
          }
        else
          {
            put_row (l, [&] (int x) {int const v = chans[0][x]; return qRgb (v, v, v);});
          }
        break;
      case Colour::Robot72:
        put_row (l, [&] (int x) {return yuv (chans[0][x], chans[2][x], chans[1][x]);});
        break;
      case Colour::PD:
        put_row (2 * l, [&] (int x) {return yuv (chans[0][x], chans[2][x], chans[1][x]);});
        put_row (2 * l + 1, [&] (int x) {return yuv (chans[3][x], chans[2][x], chans[1][x]);});
        break;
      }
  }

  void Decoder::finish (bool complete)
  {
    if (!mode_ || Picture != state_) return;
    // with the whole picture in, draw it again with the final line timing
    if (slant_fix_ && sync_line_.size () >= 6)
      {
        for (int l = 0; l < lines_decoded_; ++l) render_line (l, line_start (l));
      }
    state_ = FskId;
    // an FSK ID follows the last line
    fsk_from_ = complete ? qint64 (line_start (lines_decoded_ - 1) + samples_per_line_) : total_;
    Q_EMIT progress (rows_done_, mode_->height);
    Q_EMIT finished (image_, mode_->name, complete);
  }

  void Decoder::stop ()
  {
    if (Picture == state_) finish (false);
  }

  double Decoder::seconds_left () const
  {
    if (!mode_ || Picture != state_) return 0.;
    return (mode_->tx_lines - lines_decoded_) * mode_->line_ms () / 1000.;
  }

  int Decoder::sync_quality () const
  {
    return found_recent_ * 5;
  }

  void Decoder::look_for_fsk_id ()
  {
    qint64 const first = fsk_from_ / 12 - ms_base_;
    qint64 const n = qint64 (ms_.size ());
    if (first < 0 || first >= n) return;
    auto at = [&] (qint64 k) -> double {
      double sum = 0.;
      for (qint64 i = k - 4; i <= k + 4; ++i) sum += ms_[size_t (qBound<qint64> (0, i, n - 1))];
      return sum / 9.;
    };
    for (int one_is_1900 = 1; one_is_1900 >= 0; --one_is_1900)
      {
        for (qint64 s = first; s < qMin (n - 200, first + 1500); s += 2)
          {
            QVector<int> symbols;
            bool bad = false;
            for (int sym = 0; sym < 16 && !bad; ++sym)
              {
                int v = 0;
                for (int b = 0; b < 6; ++b)
                  {
                    qint64 const c = s + 11 + 22 * (sym * 6 + b);
                    if (c >= n) {bad = true; break;}
                    double const hz = at (c);
                    bool const near1900 = std::fabs (hz - 1900.) < 70., near2100 = std::fabs (hz - 2100.) < 70.;
                    if (!near1900 && !near2100) {bad = true; break;}
                    if (near1900 == bool (one_is_1900)) v |= 1 << b;
                  }
                if (bad) break;
                symbols << v;
                if (symbols.size () >= 3 && 0x01 == v) break;
              }
            if (symbols.size () < 6 || symbols[0] != 0x20 || symbols[1] != 0x2A || symbols.last () != 0x01) continue;
            QString call;
            for (int i = 2; i < symbols.size () - 1; ++i)
              {
                QChar const c (symbols[i] + 0x20);
                if (!(c.isDigit () || (c >= 'A' && c <= 'Z') || '/' == c)) {call.clear (); break;}
                call += c;
              }
            if (call.size () >= 3)
              {
                Q_EMIT call_heard (call);
                return;
              }
          }
      }
  }
}
