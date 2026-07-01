#include "DialogueStore.hpp"

#include <utility>

namespace
{
// Shared empty tree returned by Get() for invalid handles.
const DialogueTree& EmptyTree()
{
    static const DialogueTree empty;
    return empty;
}
}  // namespace

DialogueHandle DialogueStore::Add(DialogueTree tree)
{
    const DialogueId id = m_NextId++;
    m_Trees.emplace(id, std::move(tree));
    return DialogueHandle{id};
}

bool DialogueStore::IsValid(DialogueHandle handle) const
{
    return handle.id != 0 && m_Trees.find(handle.id) != m_Trees.end();
}

bool DialogueStore::HasTree(DialogueHandle handle) const
{
    auto it = m_Trees.find(handle.id);
    return it != m_Trees.end() && !it->second.nodes.empty();
}

const DialogueTree& DialogueStore::Get(DialogueHandle handle) const
{
    auto it = m_Trees.find(handle.id);
    return it != m_Trees.end() ? it->second : EmptyTree();
}
