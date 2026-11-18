#ifndef SSTV_CODEC_HPP__
#define SSTV_CODEC_HPP__

// Slow-scan TV: the common analogue picture modes (Martin, Scottie,
// Robot and PD), a receiver that finds the VIS header, follows the line
// syncs and draws the picture as it arrives, and a transmitter that turns
// a picture into audio. An optional FSK ID (the sender's call, sent after
// the picture the way MMSSTV does) is read and written too.

#include <QObject>
#include <QImage>
#include <QVector>
#include <QString>
#include <vector>
#include <complex>

namespace sstv
{
  enum class Colour {GBR, Robot36, Robot72, PD};

  struct Segment
  {
    enum Kind {Tone, Channel} kind;
    double ms;                  // length
    double hz;                  // for a Tone
    int channel;                // for a Channel
  };

  struct Mode
  {
    QString name;
    int vis;
    int width;
    int height;                 // picture lines
    int tx_lines;               // lines sent (PD sends two picture lines in one)
    Colour colour;
    double pre_sync_ms;         // Scottie: a sync pulse before the first line
    QVector<Segment> segments;  // one sent line
    double line_ms () const;
    double sync_offset_ms () const;   // where the sync pulse starts in a line
    double sync_ms () const;
    double seconds () const;          // whole picture, without the header
  };

  QVector<Mode> const& modes ();
  Mode const * mode_by_vis (int vis);
  Mode const * mode_by_name (QString const&);

  // a picture as 48 kHz audio (-1..1): calibration header, VIS, the lines
  // and, if call is not empty, an FSK ID
  QVector<float> encode (QImage const& picture, Mode const&, QString const& call, double sample_rate = 48000.);

  class Decoder : public QObject
  {
    Q_OBJECT

  public:
    explicit Decoder (QObject * parent = nullptr);

    // 12 kHz samples from the radio
    void add_samples (short const * samples, int count);

    void set_auto_start (bool on) {auto_start_ = on;}
    void set_slant_fix (bool on) {slant_fix_ = on;}
    void stop ();               // give up on the picture coming in

    bool receiving () const {return state_ == Picture;}
    QImage picture () const {return image_;}
    Mode const * mode () const {return mode_;}
    int lines_done () const {return rows_done_;}
    double seconds_left () const;
    int sync_quality () const;  // 0..100 over the last lines

    Q_SIGNAL void started (QString const& mode_name);
    Q_SIGNAL void progress (int rows_done, int rows);
    Q_SIGNAL void finished (QImage const&, QString const& mode_name, bool complete);
    Q_SIGNAL void call_heard (QString const& call);   // from an FSK ID

  private:
    enum State {Listening, Picture, FskId};

    void process_ms (double hz);
    void start_picture (Mode const&, qint64 start_sample);
    void decode_lines (bool final_pass);
    void render_line (int line, double start);
    double line_start (int line) const;
    void finish (bool complete);
    void look_for_fsk_id ();
    void trim ();
    float freq_at (qint64 sample) const;
    double mean_freq (double from, double to) const;
    int value_at (double from, double to) const;

    // demodulator
    std::vector<float> fir_;
    std::vector<std::complex<float>> hist_;
    int hist_pos_ {0};
    double osc_ {0.};
    std::complex<float> last_ {1.f, 0.f};

    // phase step and smoothed frequency of every sample since base_
    std::vector<std::complex<float>> disc_;
    std::complex<float> run_sum_ {0.f, 0.f};
    std::vector<float> freq_;
    std::vector<int> low_;          // running count of samples below 1350 Hz
    qint64 base_ {0};
    qint64 total_ {0};

    // VIS search, in 1 ms steps
    std::complex<float> ms_acc_ {0.f, 0.f};
    int ms_count_ {0};
    std::vector<float> ms_;
    qint64 ms_base_ {0};

    // the picture coming in
    State state_ {Listening};
    bool auto_start_ {true};
    bool slant_fix_ {true};
    Mode const * mode_ {nullptr};
    double origin_ {0.};            // sample where line 0 starts
    double samples_per_line_ {0.};
    QVector<double> sync_line_;     // lines whose sync was found
    QVector<double> sync_pos_;      // and where
    double fit_a_ {0.}, fit_b_ {0.};
    int lines_decoded_ {0};
    int missed_ {0};
    int found_recent_ {0};
    QVector<QVector<QVector<quint8>>> channels_;   // [line][channel][pixel]
    QImage image_;
    int rows_done_ {0};
    qint64 fsk_from_ {0};
  };
}

#endif
