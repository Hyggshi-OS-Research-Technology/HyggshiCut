#include "CommandHistory.h"

namespace Core {

CommandHistory::CommandHistory(size_t maxHistorySize)
    : m_maxHistorySize(maxHistorySize) {}

void CommandHistory::pushAndExecute(std::unique_ptr<ICommand> command) {
    if (!command) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Execute the command first
    command->execute();

    // If we are in the middle of history stack, truncate redo history
    if (m_currentIndex < m_history.size()) {
        m_history.erase(m_history.begin() + m_currentIndex, m_history.end());
    }

    m_history.push_back(std::move(command));

    // Maintain max history size. When the front-most (oldest) entry is
    // dropped, the undo cursor must shift down by one so it keeps pointing
    // "just past the last executed command". Without this the index drifts
    // out of range after enough commands are pushed past the cap.
    if (m_history.size() > m_maxHistorySize) {
        m_history.erase(m_history.begin());
        if (m_currentIndex > 0) --m_currentIndex;
    } else {
        ++m_currentIndex;
    }
}

bool CommandHistory::undo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentIndex == 0) return false;

    m_currentIndex--;
    m_history[m_currentIndex]->undo();
    return true;
}

bool CommandHistory::redo() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentIndex >= m_history.size()) return false;

    m_history[m_currentIndex]->execute();
    m_currentIndex++;
    return true;
}

bool CommandHistory::canUndo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentIndex > 0;
}

bool CommandHistory::canRedo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentIndex < m_history.size();
}

void CommandHistory::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
    m_currentIndex = 0;
}

std::string CommandHistory::getUndoName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentIndex > 0) {
        return m_history[m_currentIndex - 1]->getName();
    }
    return "";
}

std::string CommandHistory::getRedoName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentIndex < m_history.size()) {
        return m_history[m_currentIndex]->getName();
    }
    return "";
}

} // namespace Core