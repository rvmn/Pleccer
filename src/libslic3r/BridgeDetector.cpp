#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include <algorithm>

namespace Slic3r {

BridgeDetector::BridgeDetector(
    ExPolygon         _expolygon,
    const ExPolygons &_lower_slices, 
    coord_t           _spacing) :
    // The original infill polygon, not inflated.
    expolygons(expolygons_owned),
    // All surfaces of the object supporting this region.
    lower_slices(_lower_slices),
    spacing(_spacing)
{
    this->expolygons_owned.push_back(std::move(_expolygon));
    initialize();
}

BridgeDetector::BridgeDetector(
    const ExPolygons  &_expolygons,
    const ExPolygons  &_lower_slices,
    coord_t            _spacing) : 
    // The original infill polygon, not inflated.
    expolygons(_expolygons),
    // All surfaces of the object supporting this region.
    lower_slices(_lower_slices),
    spacing(_spacing)
{
    initialize();
}

void BridgeDetector::initialize()
{
    // 5 degrees stepping
    this->resolution = PI/(36.0*5.); 
    // output angle not known
    this->angle = -1.;

    // Outset our bridge by an arbitrary amout; we'll use this outer margin for detecting anchors.
    Polygons grown = offset(this->expolygons, float(this->spacing*.8));
    // Detect possible anchoring edges of this bridging region.
    // Detect what edges lie on lower slices by turning bridge contour and holes
    // into polylines and then clipping them with each lower slice's contour.
    // Currently _edges are only used to set a candidate direction of the bridge (see bridge_direction_candidates()).
    Polygons contours;
    contours.reserve(this->lower_slices.size());
    for (const ExPolygon &expoly : this->lower_slices){

        contours.push_back(expoly.contour);
    }
    this->_edges = intersection_pl(to_polylines(grown), contours);
    
    #ifdef SLIC3R_DEBUG
    printf("  bridge has %zu support(s)\n", this->_edges.size());
    #endif
    
    // detect anchors as intersection between our bridge expolygon and the lower slices
    // safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some edges
    this->_anchor_regions = intersection_ex(grown, union_safety_offset(this->lower_slices));
    
    /*
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge.svg",
            expolygons      => [ $self->expolygon ],
            red_expolygons  => $self->lower_slices,
            polylines       => $self->_edges,
        );
    }
    */
}

bool BridgeDetector::detect_angle(double bridge_direction_override, const PrintRegionConfig *params)
{
    
    if (this->_edges.empty() || this->_anchor_regions.empty()){
        // The bridging region is completely in the air, there are no anchors available at the layer below.
        return false;
    }

    std::vector<BridgeDirection> candidates;
    if (bridge_direction_override == 0.) {
        candidates = bridge_direction_candidates();
    } else
        candidates.emplace_back(BridgeDirection(bridge_direction_override));

        
    if (!candidates.front()._pedestal.empty())
        this->_pedestal = (Polyline)candidates.front()._pedestal;
    if (candidates.front().has_overhang_holes)
        this->has_overhang_holes = true;//(Polylines)candidates.front()._holes;

    /*  Outset the bridge expolygon by half the amount we used for detecting anchors;
        we'll use this one to clip our test lines and be sure that their endpoints
        are inside the anchors and not on their contours leading to false negatives. */
    Polygons clip_area = offset(this->expolygons, 0.5f * float(this->spacing));
    
    /*  we'll now try several directions using a rudimentary visibility check:
        bridge in several directions and then sum the length of lines having both
        endpoints within anchors */
        
    bool have_coverage = false;
    for (size_t i_angle = 0; i_angle < candidates.size(); ++ i_angle)
    {
        const double angle = candidates[i_angle].angle;
        Lines lines;
        {
            // Get an oriented bounding box around _anchor_regions.
            BoundingBox bbox = get_extents_rotated(this->_anchor_regions, - angle);
            // Cover the region with line segments.
            lines.reserve((bbox.max.y() - bbox.min.y() + this->spacing - SCALED_EPSILON) / this->spacing);
            double s = sin(angle);
            double c = cos(angle);
            // As The lines be spaced half the line width from the edge
            // FIXME: some of the test cases may fail. Need to adjust the test cases
            for (coord_t y = bbox.min.y() + this->spacing / 2; y <= bbox.max.y(); y += this->spacing)
            //for (coord_t y = bbox.min.y(); y <= bbox.max.y(); y += this->spacing) //this is the old version
                lines.push_back(Line(
                    Point((coord_t)round(c * bbox.min.x() - s * y), (coord_t)round(c * y + s * bbox.min.x())),
                    Point((coord_t)round(c * bbox.max.x() - s * y), (coord_t)round(c * y + s * bbox.max.x()))));
        }

        //create boundingbox for anchor regions
        std::vector<BoundingBox> anchor_bb;
        for (ExPolygon& poly : this->_anchor_regions) {
            anchor_bb.emplace_back(poly.contour.bounding_box());
        }

        //compute stat on line with anchors, and their lengths.
        BridgeDirection& c = candidates[i_angle];
        std::vector<coordf_t> dist_anchored;
        {
            Lines clipped_lines = intersection_ln(lines, clip_area);
            for (size_t i = 0; i < clipped_lines.size(); ++i) {
                // this can be called 100 000 time per detect_angle, please optimise
                const Line &line = clipped_lines[i];
                bool good_line = false;
                coordf_t len = line.length();
                //is anchored?
                size_t line_a_anchor_idx = -1;
                size_t line_b_anchor_idx = -1;
                for (int i = 0; i < _anchor_regions.size(); ++i) {
                    ExPolygon& poly = this->_anchor_regions[i];
                    BoundingBox& polybb = anchor_bb[i];
                    if (polybb.contains(line.a) && poly.contains(line.a)) { // using short-circuit evaluation to test boundingbox and only then the other
                        line_a_anchor_idx = i;
                        //candidates[i_angle].angle += 1.5*M_PI;
                    }
                    if (polybb.contains(line.b) && poly.contains(line.b)) { // using short-circuit evaluation to test boundingbox and only then the other
                        line_b_anchor_idx = i;
                    }
                    if (line_a_anchor_idx < clipped_lines.size() && line_b_anchor_idx < clipped_lines.size())
                        break;
                }
                //check if the anchor search has been successful
                if ( (line_a_anchor_idx < clipped_lines.size()) & (line_b_anchor_idx < clipped_lines.size())) { // this 'if' isn't very effective (culls ~ 10% on a benchy) but it's almost free to compute
                    good_line = true;
                    //test if it's not a fake bridge
                    if (line_a_anchor_idx == line_b_anchor_idx) {
                        good_line = false;
                        //check that the line go out of the anchor into the briding area 
                        // don't call intersection_ln here, as even if we succeed to limit the number of candidates to ~100, here we can have hundreds of lines, so that means dozen of thousands of calls (or more)!
                        // add some points (at least the middle) to test, it's quick
                        Point middle_point = line.midpoint();
                        for (int i = 0; i < _anchor_regions.size(); ++i) {
                            ExPolygon& poly = this->_anchor_regions[i];
                            BoundingBox& polybb = anchor_bb[i];
                            if (!polybb.contains(middle_point) || !poly.contains(middle_point)) { // using short-circuit evaluation to test boundingbox and only then the other
                                good_line = true;
                                break;
                            }
                        }
                        // if still bad, the line is long enough to warrant two more test point? (1/2000 on a benchy)
                        if (!good_line && len > this->spacing * 10) {
                            //now test with to more points
                            Line middle_line;
                            middle_line.a = (line.a + middle_point) / 2;
                            middle_line.b = (line.b + middle_point) / 2;
                            for (int i = 0; i < _anchor_regions.size(); ++i) {
                                ExPolygon& poly = this->_anchor_regions[i];
                                BoundingBox& polybb = anchor_bb[i];
                                if (!polybb.contains(middle_line.a) || !poly.contains(middle_line.a)) { // using short-circuit evaluation to test boundingbox and only then the other
                                    good_line = true;
                                    break;
                                }
                                if (!polybb.contains(middle_line.b) || !poly.contains(middle_line.b)) { // using short-circuit evaluation to test boundingbox and only then the other
                                    good_line = true;
                                    break;
                                }
                            }
                        }
                        // If the line is still bad and is a long one, use the more costly intersection_ln. This case is rare enough to swallow the cost. (1/10000 on a benchy)
                        if (!good_line && len > this->spacing * 40) {
                            //now test with intersection_ln
                            Lines lines = intersection_ln(line, to_polygons(this->_anchor_regions));
                            good_line = lines.size() > 1;
                        }
                    }
                }
                if(good_line) {
		            this->is_bridge = true;
                    // This line could be anchored at both side and goes over the void to bridge it in its middle.
                    //store stats
                    c.total_length_anchored += len;
                    c.max_length_anchored = std::max(c.max_length_anchored, len);
                    c.nb_lines_anchored++;
                    dist_anchored.push_back(len);
                } else {
                    // this line could NOT be anchored.
                    c.total_length_free += len;
                    c.max_length_free = std::max(c.max_length_free, len);
                    c.nb_lines_free++;
                }
            }        
        }
        if (c.total_length_anchored == 0. || c.nb_lines_anchored == 0) {
            continue;
        } else {
            have_coverage = true;
            // compute median
            if (!dist_anchored.empty()) {
                std::sort(dist_anchored.begin(), dist_anchored.end());
                c.median_length_anchor = dist_anchored[dist_anchored.size() / 2];
                //if(candidates[i_angle].angle>=0.5*M_PI && candidates[i_angle].angle<=0.75*M_PI) candidates[i_angle].angle += 1.5*M_PI;
            }
            // size is 20%
        }
    }

    // if no direction produced coverage, then there's no bridge direction ?
    if (!have_coverage) {
        //try again to choose the least worse
        // use only poly contour angles
        if (bridge_direction_override == 0.) {
            candidates = bridge_direction_candidates(true);
        } else
            candidates.emplace_back(BridgeDirection(bridge_direction_override));
        for (size_t i_angle = 0; i_angle < candidates.size(); ++i_angle)
        {
            const double angle = candidates[i_angle].angle;
            //use the whole polygon
            Lines lines;
            {
                // Get an oriented bounding box around _anchor_regions.
                BoundingBox bbox = get_extents_rotated(clip_area, -angle);
                // Cover the region with line segments.
                lines.reserve((bbox.max.y() - bbox.min.y() + this->spacing - SCALED_EPSILON) / this->spacing);
                double s = sin(angle);
                double c = cos(angle);
                // The lines be spaced half the line width from the edge
                for (coord_t y = bbox.min.y() + this->spacing / 2; y <= bbox.max.y(); y += this->spacing)
                    lines.push_back(Line(
                        Point((coord_t)round(c * bbox.min.x() - s * y), (coord_t)round(c * y + s * bbox.min.x())),
                        Point((coord_t)round(c * bbox.max.x() - s * y), (coord_t)round(c * y + s * bbox.max.x()))));
            }
            //compute stat on line with anchors, and their lengths.
            BridgeDirection& c = candidates[i_angle];
            std::vector<coordf_t> dist_anchored;
            {
                Lines clipped_lines = intersection_ln(lines, clip_area);
                for (size_t i = 0; i < clipped_lines.size(); ++i) {
                    const Line& line = clipped_lines[i];
                    if (expolygons_contain(this->_anchor_regions, line.b)){// || expolygons_contain(this->_anchor_regions, line.b)) {
                        // This line has one anchor (or is totally anchored)
                        coordf_t len = line.length();
                        //store stats
                        c.total_length_anchored += len;
                        c.max_length_anchored = std::max(c.max_length_anchored, len);
                        c.nb_lines_anchored++;
                        dist_anchored.push_back(len);
                    } else {
                        // this line could NOT be anchored.
                        coordf_t len = line.length();
                        c.total_length_free += len;
                        c.max_length_free = std::max(c.max_length_free, len);
                        c.nb_lines_free++;
                    }
                }
		//if(!dist_anchored.empty()&&candidates[i_angle].angle<= 0) candidates[i_angle].angle += M_PI;
            }
            if(dist_anchored.empty()){
                // lines.clear();
                // {
                //     // Get an oriented bounding box around _anchor_regions.
                //     BoundingBox bbox = get_extents_rotated(clip_area, -angle);
                //     // Cover the region with line segments.
                //     lines.reserve((bbox.max.y() - bbox.min.y() + this->spacing - SCALED_EPSILON) / this->spacing);
                //     double s = sin(angle);
                //     double c = cos(angle);
                //     // The lines be spaced half the line width from the edge
                //     for (coord_t y = bbox.min.y() + bbox.max.y()/2 + this->spacing / 2; y <= bbox.max.y(); y += this->spacing)
                //         lines.push_back(Line(
                //             Point((coord_t)round(c * bbox.min.x() - s * y), (coord_t)round(c * y + s * bbox.min.x())),
                //             Point((coord_t)round(c * bbox.max.x() - s * y), (coord_t)round(c * y + s * bbox.max.x()))));
                // }
                //compute stat on line with anchors, and their lengths.
                //BridgeDirection& c = candidates[icandidates[i_angle].angle <= .45*PI_angle];
                //std::vector<coordf_t> dist_anchored;
                {
                    Lines clipped_lines = intersection_ln(lines, clip_area);
                    for (size_t i = 0; i < clipped_lines.size(); ++i) {
                        const Line& line = clipped_lines[i];
                        if (expolygons_contain(this->_anchor_regions, line.a)){//} || expolygons_contain(this->_anchor_regions, line.b)) {
                            // This line has one anchor (or is totally anchorecandidates[i_angle].angle += 1.5*PI;d)
                            coordf_t len = line.length();
                            //store stats
                            c.total_length_anchored += len;
                            c.max_length_anchored = std::max(c.max_length_anchored, len);
                            c.nb_lines_anchored++;
                            dist_anchored.push_back(len);
                        } else {
                            // this line could NOT be anchored.
                            coordf_t len = line.length();
                            c.total_length_free += len;
                            c.max_length_free = std::max(c.max_length_free, len);
                            c.nb_lines_free++;
                        }
                    }
		   if(!dist_anchored.empty()) candidates[i_angle].angle += M_PI;
                }
            }
            if (c.total_length_anchored == 0. || c.nb_lines_anchored == 0) {
                candidates[i_angle].angle += 1.5*PI;
                continue;
            } else {
                have_coverage = true;
                //if(candidates[i_angle].angle < .5*PI && candidates[i_angle].angle >= 0.05*M_PI) candidates[i_angle].angle += 1.5*PI;
                // compute median
                if (!dist_anchored.empty()) {
                    std::sort(dist_anchored.begin(), dist_anchored.end());
                    c.median_length_anchor = dist_anchored[dist_anchored.size() / 2];
                }


                // size is 20%
            }
        }
    }

    // if no direction produced coverage, then there's no bridge direction
    if (!have_coverage)
        return false;

    //compute global stat (max & min median & max length)
    std::vector<coordf_t> all_median_length;
    std::vector<coordf_t> all_max_length;
    for (BridgeDirection &c : candidates) {
        all_median_length.push_back(c.median_length_anchor);
        all_max_length.push_back(c.max_length_anchored);
    }
    std::sort(all_median_length.begin(), all_median_length.end());
    std::sort(all_max_length.begin(), all_max_length.end());
    coordf_t median_max_length = all_max_length[all_max_length.size() / 2];
    coordf_t min_max_length = all_max_length.front();
    coordf_t max_max_length = all_max_length.back();
    coordf_t median_median_length = all_median_length[all_median_length.size() / 2];
    coordf_t min_median_length = all_median_length.front();
    coordf_t max_median_length = all_median_length.back();

    //compute individual score
    for (BridgeDirection& c : candidates) {
        c.coverage = 0;
        //ratio_anchored is 70% of the score
        double ratio_length_anchored = c.total_length_anchored / (c.total_length_anchored + c.total_length_free);
        c.coverage = params->bds_ratio_length.value * ratio_length_anchored;
        double ratio_anchored = c.nb_lines_anchored / (c.nb_lines_anchored + c.nb_lines_free);
        c.coverage += params->bds_ratio_nr.value * ratio_anchored;
        //median is 35% (and need to invert it)
        double ratio_median = 1 - double(c.median_length_anchor - min_median_length) / (double)std::max(1., max_median_length - min_median_length);
        c.coverage += params->bds_median_length.value * ratio_median;
        //max is 0% (and need to invert it)
        double ratio_max = 1 - double(c.max_length_anchored - min_max_length) / (double)std::max(1., max_max_length - min_max_length);
        c.coverage += params->bds_max_length.value * ratio_max;
        //bonus for perimeter dir
        if (c.along_perimeter_length > 0)
            c.coverage += 20;

    }
    
    // if any other direction is within extrusion width of coverage, prefer it if shorter
    // shorter = shorter max length, or if in espilon (10) range, the shorter mean length.
    // TODO: There are two options here - within width of the angle with most coverage, or within width of the currently perferred?
    size_t i_best = 0;
    for (size_t i = 1; i < candidates.size(); ++ i)
        if (candidates[i].coverage > candidates[i_best].coverage)
            i_best = i;

    this->angle = candidates[i_best].angle;//  - M_PI;
    if (this->angle >= 2*PI)
        this->angle -= 2*PI;
    else if (this->angle < 0)
	this->angle += 2*PI;

    if(params->overhang_distance>0 && !this->is_bridge  && !this->has_overhang_holes  && this->_pedestal.empty() && candidates[i_best].max_length_anchored/1000000 > float(params->overhang_distance.value)) this->is_bridge = true;

    #ifdef SLIC3R_DEBUG
    printf("  Optimal infill angle is %d degrees\n", (int)Slic3r::Geometry::rad2deg(this->angle));
    #endif

    return true;
}

std::vector<BridgeDetector::BridgeDirection> BridgeDetector::bridge_direction_candidates(bool only_from_polygon) const
{
    std::vector<BridgeDirection> angles;
    Polylines pedestals_or_holes;
    bool has_overhang_holes = false;
    Polyline pedestal;

    // we also test angles according to configured resolution
    if (!only_from_polygon)
        for (int i = 0; i <= PI/this->resolution; ++i)
            angles.emplace_back(i * this->resolution);

    // we also test angles of each bridge contour
    {
        Lines lines = to_lines(this->expolygons);
        //if many lines, only takes the bigger ones.
        float mean_sqr_size = 0;
        if (lines.size() > 20) {
            for (int i = 0; i < 20; i++) {
                mean_sqr_size += (float)lines[i].a.distance_to_square(lines[i].b);
            }
            mean_sqr_size /= 20;
            for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
                float dist_sqr = line->a.distance_to_square(line->b);
                if (dist_sqr > mean_sqr_size)
                    angles.emplace_back(line->direction(), dist_sqr);
            }
        }else
            for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
                angles.emplace_back(line->direction(), line->a.distance_to_square(line->b));
    }
    
    /*  we test angles of each open supporting edge
        (this finds the optimal angle for C-shaped supports) */
        //ExPolygon extmp;
    for (const Polyline &edge : this->_edges){
        if (edge.first_point() != edge.last_point())
            angles.emplace_back(Line(edge.first_point(), edge.last_point()).direction());
        else if(this->expolygons.data()->contains_h(edge.bounding_box().center()) && this->expolygons.data()->contains_h(edge.first_point()) && !this->expolygons.data()->contains(edge.first_point()))// && this->expolygons.data()->contains_h(edge.first_point()))// /*&& this->expolygons.data()->on_boundary(edge.first_point(),0.3)*/ && edge.bounding_box().contains(this->expolygons.data()->contour.bounding_box().center()))
        	pedestals_or_holes.emplace_back((Polyline)edge);
    }
    if(!pedestals_or_holes.empty()){
    	for (const ExPolygon& poly : this->_anchor_regions) {
            	// Check if pedestal that is found is also intersecting with an anchor region
		// if not it is not a pedestal but a hole
		for(Polyline pedhole : pedestals_or_holes){
            		if(!poly.has_boundary_point(pedhole.first_point()))
                		has_overhang_holes = true;//holes.emplace_back(pedhole);
    			else
				pedestal = pedhole;
		}
	}
	pedestals_or_holes.clear();
    }

    // remove duplicates
    std::sort(angles.begin(), angles.end(), [](const BridgeDirection& bt1, const BridgeDirection& bt2) { return bt1.angle < bt2.angle; });

    //first delete angles too close to an angle from a perimeter  
    for (size_t i = 1; i < angles.size(); ++i) {
        if (angles[i - 1].along_perimeter_length > 0 && angles[i].along_perimeter_length == 0)
            if (Slic3r::Geometry::directions_parallel(angles[i].angle, angles[i - 1].angle, this->resolution)) {
                angles.erase(angles.begin() + i);
                --i;
                continue;
            }
        if (angles[i].along_perimeter_length > 0 && angles[i - 1].along_perimeter_length == 0)
            if (Slic3r::Geometry::directions_parallel(angles[i].angle, angles[i - 1].angle, this->resolution)) {
                angles.erase(angles.begin() + (i-1));
                --i;
                continue;
            }
    }
    //then delete angle to close to each other (high resolution)
    double min_resolution = this->resolution / 8;
    for (size_t i = 1; i < angles.size(); ++i) {
        if (Slic3r::Geometry::directions_parallel(angles[i].angle, angles[i - 1].angle, min_resolution)) {
            // keep the longest of the two.
            if (angles[i].along_perimeter_length < angles[i - 1].along_perimeter_length) {
                angles.erase(angles.begin() + i);
                --i;
            } else {
                angles.erase(angles.begin() + (i-1));
                --i;
            }
        }
    }
    //then, if too much angles, delete more
    while (angles.size() > 20) {
        min_resolution *= 2;
        for (size_t i = 1; i < angles.size(); ++i) {
            if (Slic3r::Geometry::directions_parallel(angles[i].angle, angles[i - 1].angle, min_resolution)) {
                // keep the longest of the two.
                if (angles[i].along_perimeter_length < angles[i - 1].along_perimeter_length) {
                    angles.erase(angles.begin() + i);
                    --i;
                } else {
                    angles.erase(angles.begin() + (i - 1));
                    --i;
                }
            }
        }
    }
    /*  compare first value with last one and remove the greatest one (PI) 
        in case they are parallel (PI, 0) */
    if (angles.size() > 1 && Slic3r::Geometry::directions_parallel(angles.front().angle, angles.back().angle, min_resolution))
        angles.pop_back();
    angles.front()._pedestal = std::move((Polyline)pedestal);
    angles.front().has_overhang_holes = has_overhang_holes;//std::move((Polylines)holes);
    return angles;
}

/*
static void get_trapezoids(const ExPolygon &expoly, Polygons* polygons) const
{
    ExPolygons expp;
    expp.push_back(expoly);
    boost::polygon::get_trapezoids(*polygons, expp);
}

void ExPolygon::get_trapezoids(ExPolygon clone, Polygons* polygons, double angle) const
{
    clone.rotate(PI/2 - angle, Point(0,0));
    clone.get_trapezoids(polygons);
    for (Polygons::iterator polygon = polygons->begin(); polygon != polygons->end(); ++polygon)
        polygon->rotate(-(PI/2 - angle), Point(0,0));
}
*/

// This algorithm may return more trapezoids than necessary
// (i.e. it may break a single trapezoid in several because
// other parts of the object have x coordinates in the middle)
static void get_trapezoids2(const ExPolygon& expoly, Polygons* polygons)
{
    Polygons     src_polygons = to_polygons(expoly);
    // get all points of this ExPolygon
    const Points pp = to_points(src_polygons);

    // build our bounding box
    BoundingBox bb(pp);

    // get all x coordinates
    std::vector<coord_t> xx;
    xx.reserve(pp.size());
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p)
        xx.push_back(p->x());
    std::sort(xx.begin(), xx.end());

    // find trapezoids by looping from first to next-to-last coordinate
    Polygons rectangle;
    rectangle.emplace_back(Polygon());
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end()-1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x != next_x) {
            // intersect with rectangle
            // append results to return value
            rectangle.front() = { { *x, bb.min.y() }, { next_x, bb.min.y() }, { next_x, bb.max.y() }, { *x, bb.max.y() } };
            polygons_append(*polygons, intersection(rectangle, src_polygons));
        }
    }
}

static void get_trapezoids2(const ExPolygon &expoly, Polygons* polygons, double angle)
{
    ExPolygon clone = expoly;
    clone.rotate(PI/2 - angle, Point(0,0));
    get_trapezoids2(clone, polygons);
    for (Polygon &polygon : *polygons)
        polygon.rotate(-(PI/2 - angle), Point(0,0));
}



void get_trapezoids3_half(const ExPolygon& expoly, Polygons* polygons, float spacing)
{

    // get all points of this ExPolygon
    Points pp = expoly.contour.points;

    if (pp.empty()) return;

    // build our bounding box
    BoundingBox bb(pp);

    // get all x coordinates
    coord_t min_x = pp[0].x(), max_x = pp[0].x();
    std::vector<coord_t> xx;
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        if (min_x > p->x()) min_x = p->x();
        if (max_x < p->x()) max_x = p->x();
    }
    for (coord_t x = min_x; x < max_x - (coord_t)(spacing / 2); x += (coord_t)spacing) {
        xx.push_back(x);
    }
    xx.push_back(max_x);
    //std::sort(xx.begin(), xx.end());

    // find trapezoids by looping from first to next-to-last coordinate
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end() - 1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x == next_x) continue;

        // build rectangle
        Polygon poly;
        poly.points.resize(4);
        poly[0].x() = *x + (coord_t)spacing / 4;
        poly[0].y() = bb.min(1);
        poly[1].x() = next_x - (coord_t)spacing / 4;
        poly[1].y() = bb.min(1);
        poly[2].x() = next_x - (coord_t)spacing / 4;
        poly[2].y() = bb.max(1);
        poly[3].x() = *x + (coord_t)spacing / 4;
        poly[3].y() = bb.max(1);

        // intersect with this expolygon
        // append results to return value
        polygons_append(*polygons, intersection(Polygons{ poly }, to_polygons(expoly)));
    }
}

Polygons BridgeDetector::coverage(double angle, bool precise) const
{
    if (angle == -1)
        angle = this->angle;
    
    Polygons covered;
    if (angle != -1) {
        // Get anchors, convert them to Polygons and rotate them.
        Polygons anchors = to_polygons(this->_anchor_regions);
        polygons_rotate(anchors, PI / 2.0 - angle);
        //same for region which do not need bridging
        //Polygons supported_area = diff(this->lower_slices.expolygons, this->_anchor_regions, true);
        //polygons_rotate(anchors, PI / 2.0 - angle);

        for (ExPolygon expolygon : this->expolygons) {
            // Clone our expolygon and rotate it so that we work with vertical lines.
            expolygon.rotate(PI / 2.0 - angle);
            // Outset the bridge expolygon by half the amount we used for detecting anchors;
            // we'll use this one to generate our trapezoids and be sure that their vertices
            // are inside the anchors and not on their contours leading to false negatives.
            for (ExPolygon &expoly : offset_ex(expolygon, 0.5f * float(this->spacing))) {
                // Compute trapezoids according to a vertical orientation
                Polygons trapezoids;
                if (!precise) get_trapezoids2(expoly, &trapezoids, PI / 2);
                else get_trapezoids3_half(expoly, &trapezoids, float(this->spacing));
                for (Polygon &trapezoid : trapezoids) {
                    size_t n_supported = 0;
                    if (!precise) {
                        // not nice, we need a more robust non-numeric check
                        // imporvment 1: take into account when we go in the supported area.
                        for (const Line &supported_line : intersection_ln(trapezoid.lines(), anchors))
                            if (supported_line.length() >= this->spacing)
                                ++n_supported;
                    } else {
                        Polygons intersects = intersection(Polygons{trapezoid}, anchors);
                        n_supported = intersects.size();

                        if (n_supported >= 2) {
                            // trim it to not allow to go outside of the intersections
                            BoundingBox center_bound = intersects[0].bounding_box();
                            coord_t min_y = center_bound.center()(1), max_y = center_bound.center()(1);
                            for (Polygon &poly_bound : intersects) {
                                center_bound = poly_bound.bounding_box();
                                if (min_y > center_bound.center()(1)) min_y = center_bound.center()(1);
                                if (max_y < center_bound.center()(1)) max_y = center_bound.center()(1);
                            }
                            coord_t min_x = trapezoid[0](0), max_x = trapezoid[0](0);
                            for (Point &p : trapezoid.points) {
                                if (min_x > p(0)) min_x = p(0);
                                if (max_x < p(0)) max_x = p(0);
                            }
                            //add what get_trapezoids3 has removed (+EPSILON)
                            min_x -= (this->spacing / 4 + 1);
                            max_x += (this->spacing / 4 + 1);
                            coord_t mid_x = (min_x + max_x) / 2;
                            for (Point &p : trapezoid.points) {
                                if (p(1) < min_y) p(1) = min_y;
                                if (p(1) > max_y) p(1) = max_y;
                                if (p(0) > min_x && p(0) < mid_x) p(0) = min_x;
                                if (p(0) < max_x && p(0) > mid_x) p(0) = max_x;
                            }
                        }
                    }

                    if (n_supported >= 2) {
                        //add it
                        covered.push_back(std::move(trapezoid));
                    }
                }
            }
        }
        // Unite the trapezoids before rotation, as the rotation creates tiny gaps and intersections between the trapezoids
        // instead of exact overlaps.
        covered = union_(covered);
        // Intersect trapezoids with actual bridge area to remove extra margins and append it to result.
        polygons_rotate(covered, -(PI/2.0 - angle));
        //covered = intersection(this->expolygons, covered);
#if 0
        {
            my @lines = map @{$_->lines}, @$trapezoids;
            $_->rotate(-(PI/2 - $angle), [0,0]) for @lines;
            
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output(
                "coverage_" . rad2deg($angle) . ".svg",
                expolygons          => [$self->expolygon],
                green_expolygons    => $self->_anchor_regions,
                red_expolygons      => $coverage,
                lines               => \@lines,
            );
        }
#endif
    }
    return covered;
}

/*  This method returns the bridge edges (as polylines) that are not supported
    but would allow the entire bridge area to be bridged with detected angle
    if supported too */
void BridgeDetector::unsupported_edges(double angle, Polylines* unsupported) const
{
    if (angle == -1) angle = this->angle;
    if (angle == -1) return;

    Polygons grown_lower = offset(this->lower_slices, float(this->spacing));

    for (ExPolygons::const_iterator it_expoly = this->expolygons.begin(); it_expoly != this->expolygons.end(); ++ it_expoly) {    
        // get unsupported bridge edges (both contour and holes)
        Lines unsupported_lines = to_lines(diff_pl(to_polylines(*it_expoly), grown_lower));
        /*  Split into individual segments and filter out edges parallel to the bridging angle
            TODO: angle tolerance should probably be based on segment length and flow width,
            so that we build supports whenever there's a chance that at least one or two bridge
            extrusions would be anchored within such length (i.e. a slightly non-parallel bridging
            direction might still benefit from anchors if long enough)
            double angle_tolerance = PI / 180.0 * 5.0; */
        for (const Line &line : unsupported_lines)
            if (! Slic3r::Geometry::directions_parallel(line.direction(), angle)) {
                unsupported->emplace_back(Polyline());
                unsupported->back().points.emplace_back(line.a);
                unsupported->back().points.emplace_back(line.b);
            }
    }
    
    /*
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "unsupported_" . rad2deg($angle) . ".svg",
            expolygons          => [$self->expolygon],
            green_expolygons    => $self->_anchor_regions,
            red_expolygons      => union_ex($grown_lower),
            no_arrows           => 1,
            polylines           => \@bridge_edges,
            red_polylines       => $unsupported,
        );
    }
    */
}

Polylines BridgeDetector::unsupported_edges(double angle) const {
    Polylines pp;
    this->unsupported_edges(angle, &pp);
    return pp;
}

}