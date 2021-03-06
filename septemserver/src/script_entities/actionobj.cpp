#include "stdafx.h"
#include "script_entities/actionobj.h"

#include <sys/time.h>
void actionobj::DoAction()
{

    pt::ptime t2 = pt::second_clock::local_time();
    pt::time_duration diff = t2 - lastTick;

    if(diff.total_milliseconds() / 1000 > interval) {
        lastTick = t2;
        sol::protected_function_result result = func(this, user_data);
        if(!result.valid()) {
            sol::error err = result;
			// TODO: locate room or creator owner of this obj to send the message too
			
			std::stringstream ss;
			ss << "Error executing action: " << err.what();
			log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
        }
    }
}

actionobj::actionobj(sol::protected_function func, unsigned int interval, sol::object userData)
{
    this->func = func;
    this->interval = interval;
    this->user_data = userData;
    lastTick = pt::second_clock::local_time();
}

const unsigned int& actionobj::GetInterval()
{
    return interval;
}

sol::protected_function actionobj::GetFunc()
{
    return func;
}

sol::object actionobj::GetUserData()
{
    return user_data;
}
