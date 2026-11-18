#ifndef _ZROBOT_H_
#define _ZROBOT_H_

#include "zobject.h"

class ZRobot : public ZObject
{
	public:
		ZRobot(ZTime *ztime_, ZSettings *zsettings_ = NULL);
		
		static void Init();
        // Static, already team-tinted frame survives deletion of the robot.
        ZSDL_Surface *GetPredatorPreyFrame()
        {
            if(owner <= NULL_TEAM || owner >= MAX_TEAM_TYPES || direction < 0 || direction >= MAX_ANGLE_TYPES) return NULL;
            return &stand[owner][direction];
        }
		virtual void SetAttackObject(ZObject *obj);
		virtual void DoDeathEffect(bool do_fire_death, bool do_missile_death);
		virtual void FireMissile(int x_, int y_);
		virtual bool CanSetWaypoints() { return true; }
		virtual bool CanPickupGrenades() { return grenade_amount <= 0; }
		virtual bool CanHaveGrenades() { return true; }
		virtual void SetGrenadeAmount(int grenade_amount_);
		virtual int GetGrenadeAmount() { return grenade_amount; }
		virtual bool CanThrowGrenades();
		virtual void DoPickupGrenadeAnim();
	protected:
		void RecalcDirection();
		void RenderRobotSurface(ZMap &the_map, ZSDL_Surface *surface, int shift_x, int shift_y);
		void Common_Process(double the_time);
		int action_i;
		int grenade_i;
		bool throw_grenade;

		double next_grenade_time;

		int submerge_amount;

		int grenade_amount;
		
		static ZSDL_Surface null_img;

		static ZSDL_Surface stand[MAX_TEAM_TYPES][MAX_ANGLE_TYPES];
		static ZSDL_Surface throw_something[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][4];

		static ZSDL_Surface walk[MAX_TEAM_TYPES][MAX_ANGLE_TYPES][4];

		static ZSDL_Surface beer[MAX_TEAM_TYPES][10];

		static ZSDL_Surface cigarette[MAX_TEAM_TYPES][11];

		static ZSDL_Surface full_area_scan[MAX_TEAM_TYPES][12];
		static ZSDL_Surface head_stretch[MAX_TEAM_TYPES][11];

		static ZSDL_Surface pickup_down[MAX_TEAM_TYPES][4];
		static ZSDL_Surface pickup_up[MAX_TEAM_TYPES][4];

		
};

#endif
