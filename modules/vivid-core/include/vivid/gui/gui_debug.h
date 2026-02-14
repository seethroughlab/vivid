#pragma once

/**
 * @file gui_debug.h
 * @brief Debug logging and validation utilities for the GUI system
 *
 * Provides infrastructure for debugging panel state transitions,
 * validating consistency between state locations, and dumping state.
 *
 * Enable debug mode via setDebugEnabled(true) or by defining VIVID_GUI_DEBUG.
 */

#include <string>

namespace vivid {

// Forward declarations
class PanelManager;
class Panel;

namespace gui {

/**
 * @brief Enable or disable GUI debug logging
 * @param enabled If true, log state transitions to stderr
 *
 * Can also be enabled at compile time with VIVID_GUI_DEBUG.
 */
void setDebugEnabled(bool enabled);

/**
 * @brief Check if GUI debug logging is enabled
 */
bool isDebugEnabled();

/**
 * @brief Log a state transition
 * @param component Component name (e.g., "PanelManager")
 * @param from Previous state description
 * @param to New state description
 * @param reason Reason for transition (panel ID, etc.)
 *
 * Output format: [GUI:Component] from -> to (reason)
 */
void logTransition(const char* component, const char* from, const char* to, const char* reason);

/**
 * @brief Log a debug message
 * @param component Component name
 * @param message Debug message
 */
void logDebug(const char* component, const char* message);

/**
 * @brief Log a debug message with a value
 * @param component Component name
 * @param message Debug message
 * @param value Associated value (panel ID, etc.)
 */
void logDebug(const char* component, const char* message, const std::string& value);

/**
 * @brief Dump current panel states to stderr
 * @param pm PanelManager to dump
 */
void dumpPanelStates(const PanelManager& pm);

/**
 * @brief Validate state consistency
 * @param pm PanelManager to validate
 * @return Number of issues found (0 = consistent)
 */
int validateState(const PanelManager& pm);

} // namespace gui
} // namespace vivid
