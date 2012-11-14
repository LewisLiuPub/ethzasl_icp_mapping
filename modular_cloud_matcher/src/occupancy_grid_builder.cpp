#include "pointmatcher_ros/get_params_from_server.h"
#include "gridmap/grid-map.h"
#include "gridmap/grid-functors.h"
#include "tf/transform_listener.h"
#include "sensor_msgs/LaserScan.h"

using namespace std;

static const signed short laserScanUpdateTable[21] = { -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, -8000, 8000 };

struct OccupancyGridBuilder
{
	ros::NodeHandle nh;
	tf::TransformListener tfListener;
	std::string mapFrame;
	const float maxRange;
	const int obsVal;
	GridMap::Group mapGroup;
	GridMap probMap;
	GridMap knownMap;
	ros::Publisher mapPub;
	ros::Subscriber laserScanSub;
	
	OccupancyGridBuilder() :
		mapFrame(getParam<string>("map_frame", "map")),
		maxRange(getParam<double>("max_range", 80.)),
		obsVal(getParam<int>("obs_value", 8000)),
		probMap(getParam<double>("resolution", 0.05), 0, &mapGroup),
		knownMap(&mapGroup, -1),
		mapPub(nh.advertise<nav_msgs::OccupancyGrid>("map", 2, true)),
		laserScanSub(nh.subscribe("scan", 10, &OccupancyGridBuilder::scanReceived, this))
	{
	}
	
	void scanReceived(const sensor_msgs::LaserScan& scan)
	{
		typedef GridMap::Vector Vector;
		MapConstUpdater probUpdater(probMap, -obsVal);
		Drawer knownUpdater(knownMap);

		const ros::Time stamp(scan.header.stamp);
		tf::StampedTransform transform;
		assert(scan.ranges.size());
		float angle(scan.angle_min);
		for (size_t a = 0; a < scan.ranges.size(); ++a) // angles indice
		{
			const float range(scan.ranges[a]);
			if (range >= scan.range_min && range <= scan.range_max)
			{
				const ros::Time scanStamp(stamp + ros::Duration(scan.time_increment)*a);
				// get transform
				tfListener.waitForTransform(mapFrame, scan.header.frame_id, scanStamp, ros::Duration(1.0), ros::Duration(0.1)); 
				tfListener.lookupTransform(mapFrame, scan.header.frame_id, scanStamp, transform);
				// compute ray positions
				const tf::Vector3 rayStart = transform(tf::Vector3(0,0,0));
				const float dist(min(range, maxRange));
				const tf::Vector3 rayEnd = transform(tf::Vector3(cosf(angle) * dist, sinf(angle) * dist, 0));
				// update map
				probMap.lineScan(
					Vector(rayStart.x(), rayStart.y()), 
					Vector(rayEnd.x(), rayEnd.y()),
					probUpdater
				);
				if (range < maxRange)
					probMap.addNearestValueSaturated(Vector(rayEnd.x(), rayEnd.y()), 2*obsVal);
				knownMap.lineScan(
					Vector(rayStart.x(), rayStart.y()), 
					Vector(rayEnd.x(), rayEnd.y()),
					knownUpdater,
					0
				);
			}
			angle += scan.angle_increment;
		}
		
		mapPub.publish(probMap.toOccupancyGrid(mapFrame, &knownMap));
	}
};

int main(int argc, char* argv[])
{
	ros::init(argc, argv, "occupancy_grid_builder");
	OccupancyGridBuilder ogb;
	while (ros::ok())
	{
		try
		{
			ros::spin();
		}
		catch (const tf::ExtrapolationException& e)
		{
			ROS_WARN_STREAM(e.what());
		}
	}
	return 0;
}