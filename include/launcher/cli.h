#pragma once

#include "wx/wxchar.h"

class CLIParser {
public:
    int Parse(int argc, wxChar** argv);

    inline bool ForceWizard() const {
        return _forceWizard;
    }

    inline bool ForceRepentogonUpdate() const {
        return _forceRepentogonUpdate;
    }

private:
    bool _forceWizard = false;
    bool _forceRepentogonUpdate = false;

    static constexpr const char* forceWizard = "force-wizard";
    static constexpr const char* forceRepentogonUpdate = "force-repentogon-update";
    /* static constexpr const char* help = "help";
    static constexpr const char* helpShort = "h"; */
};