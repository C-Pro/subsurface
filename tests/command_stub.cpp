// SPDX-License-Identifier: GPL-2.0
#include <string>

// We don't link the undo-code into most tests, so we provide
// this placeholder library for Command::changesMade(), which is needed
// by the git storage code. Tests that link subsurface_commands do not need this.
namespace Command {
	std::string changesMade() { return {}; }
}
