//

#include "FileStreamManager.h"
#include "Controller.h"

using namespace NaCs;

namespace Spcm{

bool FileStreamManager::reqRestart(uint32_t trig_id) {
    if (trig_id == restart_id) {
        // restart already requested
        return false;
    }
    restart_id = trig_id;
    m_ctrl.reqRestart(trig_id);
    return true;
}


}
