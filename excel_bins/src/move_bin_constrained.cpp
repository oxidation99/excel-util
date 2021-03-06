#include "move_bin_constrained.h"

/*--------------------------------------------------------------------
 * MoveBin()
 * Constructor.
 *------------------------------------------------------------------*/
MoveBin::MoveBin() : group("excel") ,spinner(1)
{
	spinner.start();
	boost::shared_ptr<tf::TransformListener> tf(new tf::TransformListener(ros::Duration(2.0)));
	planning_scene_monitor::PlanningSceneMonitorPtr plg_scn_mon(new planning_scene_monitor::PlanningSceneMonitor("robot_description", tf));
	planning_scene_monitor = plg_scn_mon;

	ros::NodeHandle nh_, nh_param_("~");
	ros::WallDuration sleep_t(0.5);

	group.setPlanningTime(8.0);

	service_client = nh_.serviceClient<moveit_msgs::GetPositionIK> ("compute_ik");
	while(!service_client.exists())
	{
		ROS_INFO("Waiting for service");
		sleep(1.0);
	}

	service_request.ik_request.group_name = "excel";
	service_request.ik_request.pose_stamped.header.frame_id = "table_link";
	service_request.ik_request.avoid_collisions = true;
	service_request.ik_request.attempts = 10;

	// Loading planning_scene_monitor //
	planning_scene_monitor->startSceneMonitor();
	planning_scene_monitor->startStateMonitor();
	planning_scene_monitor->startWorldGeometryMonitor();

	// Making sure we can publish attached/unattached objects //
	attached_object_publisher = nh_.advertise<moveit_msgs::AttachedCollisionObject>("attached_collision_object", 1);
	while(attached_object_publisher.getNumSubscribers() < 1)
	{
		sleep_t.sleep();
	}
	planning_scene_diff_publisher = nh_.advertise<moveit_msgs::PlanningScene>("planning_scene", 1);
	while(planning_scene_diff_publisher.getNumSubscribers() < 1)
	{
		sleep_t.sleep();
	}
}

/*--------------------------------------------------------------------
 * move_on_top()
 * Moves to the specified bin number location
 *------------------------------------------------------------------*/
int MoveBin::move_on_top(int bin_number)
{
	std::ostringstream os;
	os << bin_number;
	std::string bin_name = "bin#" + os.str(); 

	planning_scene_monitor->requestPlanningSceneState();
	full_planning_scene = planning_scene_monitor->getPlanningScene();
	full_planning_scene->getPlanningSceneMsg(planning_scene);
	collision_objects = planning_scene.world.collision_objects;

	int bin_found = 0, object_id;
	for(int i=0;i<collision_objects.size();i++){
		if(collision_objects[i].id == bin_name){
			bin_found = 1;
			object_id = i;
		}
	}

	if (bin_found){
		std::vector<double> group_variable_values;
		group.getCurrentState()->copyJointGroupPositions(group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName()), group_variable_values);
		ROS_INFO("Moving on top of the bin");
		bin_height = collision_objects[object_id].meshes[0].vertices[0].z;


		geometry_msgs::Pose object_pick_pose = collision_objects[object_id].mesh_poses[0];
		tf::Quaternion co_quat(object_pick_pose.orientation.x, object_pick_pose.orientation.y, object_pick_pose.orientation.z, object_pick_pose.orientation.w);
		tf::Matrix3x3 m(co_quat);
		double roll, pitch, yaw;
		m.getRPY(roll, pitch, yaw);
		tf::Quaternion quat = tf::createQuaternionFromRPY(M_PI/2-yaw,M_PI/2,M_PI);
		service_request.ik_request.pose_stamped.pose.position = object_pick_pose.position;
		service_request.ik_request.pose_stamped.pose.position.z = TABLE_HEIGHT+GRIPPING_OFFSET+bin_height+DZ;
		service_request.ik_request.pose_stamped.pose.orientation.x = quat.x();
		service_request.ik_request.pose_stamped.pose.orientation.y = quat.y();
		service_request.ik_request.pose_stamped.pose.orientation.z = quat.z();
		service_request.ik_request.pose_stamped.pose.orientation.w = quat.w();

		/*
		std::vector<std::string> joints;
		joints.push_back("table_rail_joint");
		joints.push_back("shoulder_pan_joint");
		joints.push_back("shoulder_lift_joint");
		joints.push_back("elbow_joint");
		joints.push_back("wrist_1_joint");
		joints.push_back("wrist_2_joint");
		joints.push_back("wrist_3_joint");
		group_variable_values[0] = 1000;
		group_variable_values[1] = 1000;
		group_variable_values[2] = -1.95;
		group_variable_values[3] = 2.50;
		group_variable_values[4] = -2.09;
		group_variable_values[5] = -1.57;
		group_variable_values[6] = -3.17;

		service_request.ik_request.robot_state.joint_state.position = group_variable_values;
		service_request.ik_request.robot_state.joint_state.name = joints;*/
		service_client.call(service_request, service_response);
		/*
		std::vector<std::string> joints_null;
		service_request.ik_request.robot_state.joint_state.name = joints_null;
		 */		

		//group_variable_values[0] = service_response.solution.joint_state.position[0];
		//group_variable_values[1] = service_response.solution.joint_state.position[1];

		//group.setJointValueTarget(group_variable_values);

		// Fixing shoulder_pan given by the IK
		service_response.solution.joint_state.position[1] = this->optimal_goal_angle(service_response.solution.joint_state.position[1],planning_scene.robot_state.joint_state.position[1]);

		group.setJointValueTarget(service_response.solution.joint_state);
		/*for (int i=0;i<service_response.solution.joint_state.name.size();i++){
			std::cout << service_response.solution.joint_state.name[i] << std::endl;
		}*/
		group.move();
		return 1;
	}else{
		// std::string error_msg = ""+bin_name + " is not in the scene. Aborting !";
		ROS_ERROR("This bin is not in the scene.");
		return 0;
	}

}

/*--------------------------------------------------------------------
 * descent()
 * Descent to gripping height
 *------------------------------------------------------------------*/
void MoveBin::descent()
{
	ROS_INFO("Descent");

	std::vector<double> group_variable_values;
	group.getCurrentState()->copyJointGroupPositions(group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName()), group_variable_values);


	std::vector<std::string> joints;
	joints.push_back("table_rail_joint");
	joints.push_back("shoulder_pan_joint");
	group_variable_values[0] = group_variable_values[0];
	group_variable_values[1] = group_variable_values[1];

	service_request.ik_request.pose_stamped.pose.position.z = TABLE_HEIGHT+GRIPPING_OFFSET+bin_height ;

	service_request.ik_request.robot_state.joint_state.position = group_variable_values;
	service_request.ik_request.robot_state.joint_state.name = joints;

	service_client.call(service_request, service_response);
	///*
	std::vector<std::string> joints_null;
	service_request.ik_request.robot_state.joint_state.name = joints_null;
	//*/		


	group.setJointValueTarget(service_response.solution.joint_state);
	group.move();
}

/*--------------------------------------------------------------------
 * attach_bin()
 * Attaches the specified bin number to the robot
 *------------------------------------------------------------------*/
int MoveBin::attach_bin(int bin_number)
{
	std::ostringstream os;
	os << bin_number;
	std::string bin_name = "bin#" + os.str(); 

	planning_scene_monitor->requestPlanningSceneState();
	full_planning_scene = planning_scene_monitor->getPlanningScene();
	full_planning_scene->getPlanningSceneMsg(planning_scene);
	collision_objects = planning_scene.world.collision_objects;

	int bin_found = 0, object_id;
	for(int i=0;i<collision_objects.size();i++){
		if(collision_objects[i].id == bin_name){
			bin_found = 1;
			object_id = i;
		}
	}

	if (bin_found){
		ROS_INFO("Attaching the bin");
		moveit_msgs::AttachedCollisionObject attached_object;
		attached_object.link_name = "wrist_3_link";
		attached_object.object = collision_objects[object_id];
		attached_object.object.operation = attached_object.object.ADD;
		attached_object_publisher.publish(attached_object);
		return 1;
	}else{
		// std::string error_msg = ""+bin_name + " is not in the scene. Aborting !";
		ROS_ERROR("This bin is not in the scene.");
		return 0;
	}
}

/*--------------------------------------------------------------------
 * travelling_position()
 * Move to travelling position
 *------------------------------------------------------------------*/
void MoveBin::travelling_position()
{
	ROS_INFO("Travelling position");	
	service_response.solution.joint_state.position[2] = -1.89;
	service_response.solution.joint_state.position[3] = 2.39;
	service_response.solution.joint_state.position[4] = -2.06;
	service_response.solution.joint_state.position[5] = -1.57;
	service_response.solution.joint_state.position[6] = M_PI/2;
	group.setJointValueTarget(service_response.solution.joint_state);
	group.move();
}

/*--------------------------------------------------------------------
 * ascent()
 * Ascent to moving height
 *------------------------------------------------------------------*/
void MoveBin::ascent()
{
	ROS_INFO("Ascent");
	service_request.ik_request.pose_stamped.pose.position.z =  TABLE_HEIGHT+GRIPPING_OFFSET+bin_height+DZ;

	std::vector<double> group_variable_values;
	group.getCurrentState()->copyJointGroupPositions(group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName()), group_variable_values);


	std::vector<std::string> joints;
	joints.push_back("table_rail_joint");
	joints.push_back("shoulder_pan_joint");
	group_variable_values[0] = group_variable_values[0];
	group_variable_values[1] = group_variable_values[1];


	service_request.ik_request.robot_state.joint_state.position = group_variable_values;
	service_request.ik_request.robot_state.joint_state.name = joints;

	service_client.call(service_request, service_response);
	///*
	std::vector<std::string> joints_null;
	service_request.ik_request.robot_state.joint_state.name = joints_null;
	//*/	

	group.setJointValueTarget(service_response.solution.joint_state);
	group.move();
}

/*--------------------------------------------------------------------
 * carry_bin_to()
 * Moves to target location keeping the grasping orientation
 *------------------------------------------------------------------*/
void MoveBin::carry_bin_to(double x_target, double y_target, double angle_target)
{
	ROS_INFO("Carrying bin to target");

	planning_scene_monitor->requestPlanningSceneState();
	full_planning_scene = planning_scene_monitor->getPlanningScene();
	full_planning_scene->getPlanningSceneMsg(planning_scene);
	collision_objects = planning_scene.world.collision_objects;

	std::vector<double> group_variable_values;
	service_request.ik_request.pose_stamped.pose.position.z =  TABLE_HEIGHT+GRIPPING_OFFSET+bin_height ;

	group.getCurrentState()->copyJointGroupPositions(group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName()), group_variable_values);
	tf::Quaternion quat_goal = tf::createQuaternionFromRPY(M_PI/2-angle_target*M_PI/180.0,M_PI/2,M_PI);
	service_request.ik_request.pose_stamped.pose.position.x = x_target;
	service_request.ik_request.pose_stamped.pose.position.y = y_target;
	service_request.ik_request.pose_stamped.pose.orientation.x = quat_goal.x();
	service_request.ik_request.pose_stamped.pose.orientation.y = quat_goal.y();
	service_request.ik_request.pose_stamped.pose.orientation.z = quat_goal.z();
	service_request.ik_request.pose_stamped.pose.orientation.w = quat_goal.w();

	//std::vector<std::string> links;
	//links.push_back("base_link");
	//service_request.ik_request.ik_link_names = links;

	// Fixing shoulder_pan given by the IK
	service_response.solution.joint_state.position[1] = this->optimal_goal_angle(service_response.solution.joint_state.position[1],planning_scene.robot_state.joint_state.position[1]);

	service_client.call(service_request, service_response);
	group_variable_values[0] = service_response.solution.joint_state.position[0];
	group_variable_values[1] = service_response.solution.joint_state.position[1];	

	group.setJointValueTarget(group_variable_values);
	//group.setJointValueTarget(service_response.solution.joint_state);;
	group.move();
}

/*--------------------------------------------------------------------
 * move_on_top()
 * Moves on top of the target location
 *------------------------------------------------------------------*/
void MoveBin::prepare(double x_target, double y_target, double angle_target)
{
	std::vector<double> group_variable_values;

	planning_scene_monitor->requestPlanningSceneState();
	full_planning_scene = planning_scene_monitor->getPlanningScene();
	full_planning_scene->getPlanningSceneMsg(planning_scene);
	collision_objects = planning_scene.world.collision_objects;

	group.getCurrentState()->copyJointGroupPositions(group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName()), group_variable_values);
	ROS_INFO("Moving on top of final location");

	tf::Quaternion quat = tf::createQuaternionFromRPY(M_PI/2-angle_target*M_PI/180.0,M_PI/2,M_PI);
	service_request.ik_request.pose_stamped.pose.position.x = x_target;
	service_request.ik_request.pose_stamped.pose.position.y = y_target;
	service_request.ik_request.pose_stamped.pose.position.z = TABLE_HEIGHT+GRIPPING_OFFSET+bin_height+DZ;
	service_request.ik_request.pose_stamped.pose.orientation.x = quat.x();
	service_request.ik_request.pose_stamped.pose.orientation.y = quat.y();
	service_request.ik_request.pose_stamped.pose.orientation.z = quat.z();
	service_request.ik_request.pose_stamped.pose.orientation.w = quat.w();


	service_client.call(service_request, service_response);
	
	// Fixing shoulder_pan given by the IK
	service_response.solution.joint_state.position[1] = this->optimal_goal_angle(service_response.solution.joint_state.position[1],planning_scene.robot_state.joint_state.position[1]);

	group.setJointValueTarget(service_response.solution.joint_state);
	group.move();
}

/*--------------------------------------------------------------------
 * detach_bin()
 * Detaches the bin from the robot
 *------------------------------------------------------------------*/
void MoveBin::detach_bin()
{
	planning_scene_monitor->requestPlanningSceneState();
	full_planning_scene = planning_scene_monitor->getPlanningScene();
	full_planning_scene->getPlanningSceneMsg(planning_scene);

	if (planning_scene.robot_state.attached_collision_objects.size()>0){
		moveit_msgs::AttachedCollisionObject attached_object = planning_scene.robot_state.attached_collision_objects[0];
		planning_scene.robot_state.attached_collision_objects.clear();
		planning_scene.world.collision_objects.push_back(attached_object.object);
		planning_scene_diff_publisher.publish(planning_scene);
	}else{
		ROS_ERROR("There was no bin attached to the robot");
	}
}

/*--------------------------------------------------------------------
 * optimal_goal_angle()
 * Finds out if the robot needs to rotate clockwise or anti-clockwise
 *------------------------------------------------------------------*/
double MoveBin::optimal_goal_angle(double goal_angle, double current_angle)
{
	std::cout<< "Current angle is : "<<current_angle<<std::endl;
	std::cout<< "Goal angle is : "<<goal_angle<<std::endl;


	while( std::abs(std::max(current_angle,goal_angle) - std::min(current_angle,goal_angle))>M_PI){
		std::cout<<"This is not the shortest path"<<std::endl;
		if (goal_angle>current_angle){
			goal_angle -= 2*M_PI;
		}
		else{
			goal_angle += 2*M_PI;
		}

	}

	if(goal_angle>2*M_PI){
		std::cout<<"Your goal_angle would be too high"<<std::endl<<"Sorry, going the other way"<<std::endl;
		goal_angle -= 2*M_PI;
	}
	if(goal_angle<-2*M_PI){
		std::cout<<"Your goal_angle would be too small"<<std::endl<<"Sorry, going the other way"<<std::endl;
		goal_angle += 2*M_PI;
	}
	std::cout<<"Final angle is : "<< goal_angle<< std::endl;
	return goal_angle;
}

int main(int argc, char **argv)
{
	ros::init(argc, argv, "move_bin");
	usleep(1000*1000);

	MoveBin movebin;

	int run_prg = 1;

	while(run_prg){
		int nb;
		double x ,y, o;
		std::cout<< "Which bin number would you like to move ?" << std::endl;
		std::cout<< "bin#" << std::endl;
		std::cin >> nb;
		std::cout<< "Where should I put the bin ?" << std::endl;
		std::cout<< "x :" << std::endl;
		std::cin >> x;
		std::cout<< "y :" << std::endl;
		std::cin >> y;
		std::cout<< "angle :" << std::endl;
		std::cin >> o;
		if (!movebin.move_on_top(nb)){
			ROS_ERROR("Aborting !");
			continue;
		}
		//std::cout<< "Press keyboard !" << std::endl;
		//std::cin.ignore();
		movebin.descent();
		//std::cout<< "Press keyboard !" << std::endl;
		//std::cin.ignore();
		if (!movebin.attach_bin(nb)){
			ROS_ERROR("Aborting !");
			continue;
		}
		std::cout<< "Press keyboard !" << std::endl;
		std::cin.ignore();
		movebin.ascent();
		std::cout<< "Press keyboard !" << std::endl;
		std::cin.ignore();
		movebin.travelling_position();
		std::cout<< "Press keyboard !" << std::endl;
		std::cin.ignore();
		movebin.carry_bin_to(x,y,o);
		std::cout<< "Press keyboard !" << std::endl;
		std::cin.ignore();
		movebin.prepare(x,y,o);
		std::cout<< "Press keyboard !" << std::endl;
		std::cin.ignore();
		movebin.descent();
		//std::cout<< "Press keyboard !" << std::endl;
		//std::cin.ignore();
		movebin.detach_bin();
		//std::cout<< "Press keyboard !" << std::endl;
		//std::cin.ignore();
		movebin.ascent();
		//std::cout<< "Press keyboard !" << std::endl;
		//std::cin.ignore();
		movebin.travelling_position();

		std::cout<< "Keep moveing bins ? (0/1)" << std::endl;
		std::cin >> run_prg;
	}

	ros::shutdown();
}
