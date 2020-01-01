#include "timelinescaledobject.h"

#include <QtMath>

TimelineScaledObject::TimelineScaledObject() :
  scale_(1.0)
{

}

const rational &TimelineScaledObject::timebase()
{
  return timebase_;
}

const double &TimelineScaledObject::timebase_dbl()
{
  return timebase_dbl_;
}

rational TimelineScaledObject::SceneToTime(const double &x, const double &x_scale, const rational &timebase, bool round)
{
  double unscaled_time = x / x_scale / timebase.toDouble();

  // Adjust screen point by scale and timebase
  qint64 rounded_x_mvmt;

  if (round) {
    rounded_x_mvmt = qRound64(unscaled_time);
  } else {
    rounded_x_mvmt = qFloor(unscaled_time);
  }

  // Return a time in the timebase
  return rational(rounded_x_mvmt * timebase.numerator(), timebase.denominator());
}

double TimelineScaledObject::TimeToScene(const rational &time)
{
  return time.toDouble() * scale_;
}

rational TimelineScaledObject::SceneToTime(const double &x, bool round)
{
  return SceneToTime(x, scale_, timebase_, round);
}

void TimelineScaledObject::SetTimebaseInternal(const rational &timebase)
{
  timebase_ = timebase;
  timebase_dbl_ = timebase_.toDouble();
}
