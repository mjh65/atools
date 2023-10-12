/*****************************************************************************
* Copyright 2015-2023 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "polygontools.h"

#include "atools.h"
#include "geo/calculations.h"

#include <QPolygonF>
#include <limits>

namespace atools {
namespace util {

PolygonLineDistance::PolygonLineDistance(const QLineF& lineParam, double lengthParam, double angleParam, int indexFromParam,
                                         int indexToParam)
  : length(lengthParam), angle(angleParam), indexFrom(indexFromParam), indexTo(indexToParam), line(lineParam)
{
}

bool PolygonLineDistance::hasSameAngle(const PolygonLineDistance& other, double maxAngle) const
{
  return atools::geo::angleAbsDiff(angle, other.angle) < maxAngle;
}

// Find point along one edge of bounding box.
// In this case, we find smallest y; in case of tie also smallest x.
// This ensures that the point is a member of the convex hull
int findCornerPoint(const QPolygonF& polygon, int size)
{
  int minPointIndex = -1;

  double minY = std::numeric_limits<double>::max();
  double minXAtMinY = std::numeric_limits<double>::max();

  for(int i = 0; i < size; i++)
  {
    const QPointF& point = polygon.at(i);
    double y = point.y();

    if(y > minY)
      continue;

    if(atools::almostEqual(y, minY) && point.y() >= minXAtMinY)
      continue;

    // Minimum so far
    minPointIndex = i;
    minY = y;
    minXAtMinY = point.x();
  }

  return minPointIndex;
}

Orientation getPolygonOrientation(const QPolygonF& polygon)
{
  int size = polygon.size();

  // Skip last if closed polygon
  if(polygon.isClosed())
    size--;

  if(size <= 2)
    return INVALID_TOO_SMALL;

  // Find point on convex hull
  int minPt = findCornerPoint(polygon, size);

  // Orientation matrix:
  // .     [ 1 xa ya ]
  // . O = [ 1 xb yb ]
  // .     [ 1 xc yc ]
  const QPointF& a = polygon.at(atools::wrapIndex(minPt - 1, size));
  const QPointF& b = polygon.at(minPt);
  const QPointF& c = polygon.at(atools::wrapIndex(minPt + 1, size));

  // det(O) = (xb * yc + xa * yb + ya * xc) - (ya * xb + yb * xc + xa * yc)
  double det = (b.x() * c.y() + a.x() * b.y() + a.y() * c.x()) - (a.y() * b.x() + b.y() * c.x() + a.x() * c.y());
  return det > 0 ? CLOCKWISE : COUNTERCLOCKWISE;
}

bool PolygonLineDistance::isLineIntersectingRect(const QLineF& line, const QRectF& rect)
{
  return rect.contains(line.p1()) || rect.contains(line.p2()) || // Either start or end point inside rect
         // or line intersects and boundary line
         line.intersects(QLineF(rect.topLeft(), rect.topRight()), nullptr) == QLineF::BoundedIntersection ||
         line.intersects(QLineF(rect.topRight(), rect.bottomRight()), nullptr) == QLineF::BoundedIntersection ||
         line.intersects(QLineF(rect.bottomRight(), rect.bottomLeft()), nullptr) == QLineF::BoundedIntersection ||
         line.intersects(QLineF(rect.bottomLeft(), rect.topLeft()), nullptr) == QLineF::BoundedIntersection;
}

bool PolygonLineDistance::isLineInsideRect(const QLineF& line, const QRectF& rect)
{
  // Both points must be inside
  return rect.contains(line.p1()) && rect.contains(line.p2());
}

PolygonLineDistances PolygonLineDistance::createPolyLines(const QVector<QLineF>& lines, const QRectF& screenRect, int size,
                                                          bool checkIntersect, double *anglesStdDev)
{
  // Collect relative angles for later calculation of standard deviation
  QVector<double> angles;
  PolygonLineDistances distLines;
  for(int i = 0; i < size; i++)
  {
    const QLineF& line = lines.at(i);
    double angle = atools::geo::angleFromQt(line.angle());

    if(anglesStdDev != nullptr && !distLines.isEmpty())
      // Calculate relative angle
      angles.append(atools::geo::angleAbsDiff(angle, distLines.constLast().angle));

    if(checkIntersect ? isLineIntersectingRect(line, screenRect) : isLineInsideRect(line, screenRect))
      // Either fully visible or overlapping - append distance
      distLines.append(PolygonLineDistance(line, line.length(), angle, i, i + 1));
    else
      // Not visible at all - append invalid distance but keep angle for later calculation
      distLines.append(PolygonLineDistance(line, 0.f, angle, -1, -1));
  }

  if(anglesStdDev != nullptr)
  {
    *anglesStdDev = 0.;

    // Sum all values
    double sum = std::accumulate(angles.constBegin(), angles.constEnd(), 0.);

    // Average
    double avg = sum / angles.size();

    // Calculate standard deviation
    for(double angle : angles)
      *anglesStdDev += (angle - avg) * (angle - avg);
  }

  return distLines;
}

PolygonLineDistances PolygonLineDistance::getLongPolygonLines(const QPolygonF& polygon, const QRectF& screenRect, int limit, float maxAngle,
                                                              bool *circle)
{
  PolygonLineDistances distLines;
  int size = polygon.size();

  // Skip last if closed polygon
  if(polygon.isClosed())
    size--;

  if(size <= 2 || limit == 0)
    return distLines;

  // Create list of lines
  QVector<QLineF> lines;
  for(int i = 0; i < size; i++)
    lines.append(QLineF(polygon.at(atools::wrapIndex(i, size)), polygon.at(atools::wrapIndex(i + 1, size))));

  double anglesStdDev = 0.;
  // Collect lines fully visible
  distLines = createPolyLines(lines, screenRect, size, false /* checkIntersect */, circle != nullptr ? &anglesStdDev : nullptr);
  if(distLines.isEmpty())
    // Nothing found - collect lines touching screen rectangle
    distLines = createPolyLines(lines, screenRect, size, true /* checkIntersect */, circle != nullptr ? &anglesStdDev : nullptr);

  if(circle != nullptr)
    // Low standard deviation means circular polygon where all segments have the same relative angle
    *circle = anglesStdDev > 0. && anglesStdDev < 100.;

  if(!distLines.isEmpty())
  {
    // Combine lines if angle is given ====================================
    if(maxAngle > 0.)
    {
      // Find first valid entry for combining
      int firstIdx = -1;
      for(int i = 0; i < size; i++)
      {
        if(distLines.at(i).isValid())
        {
          firstIdx = i;
          break;
        }
      }

      if(firstIdx != -1)
      {
        // Now combine lines =====================================================
        PolygonLineDistances consecutiveLines({distLines.at(firstIdx)});

        // First already added above
        firstIdx++;

        for(int i = firstIdx; i < distLines.size(); i++)
        {
          const PolygonLineDistance& curLineDist = distLines.at(i);
          if(!curLineDist.isValid())
            continue;

          PolygonLineDistance& lastLineDist = consecutiveLines.last();

          if(curLineDist.hasSameAngle(lastLineDist, maxAngle))
          {
            // Same angle - adjust index, sum up length and adapt points of last
            lastLineDist.indexTo = i + 1;
            lastLineDist.length += curLineDist.length;
            lastLineDist.line.setP2(curLineDist.line.p2());
          }
          else
            // Different angle
            consecutiveLines.append(curLineDist);
        }

        // Move over
        distLines = std::move(consecutiveLines);
      }
    }

    // Sort by length or from index if length is equal
    std::sort(distLines.begin(), distLines.end(), [](const PolygonLineDistance& ld1, const PolygonLineDistance& ld2)->bool {
          return atools::almostEqual(ld1.length, ld2.length, 0.001) ? ld1.indexFrom<ld2.indexFrom : ld1.length> ld2.length;
        });

    // Prune if requested
    if(distLines.size() > limit)
      distLines.erase(std::next(distLines.begin(), limit), distLines.end());
  }

  return distLines;
}

} // namespace util
} // namespace atools
