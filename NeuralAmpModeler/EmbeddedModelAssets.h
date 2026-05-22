#pragma once

#include <cstddef>

namespace embedded_model
{
struct EmbeddedModelAsset
{
  const char* token;
  const char* json;
  size_t jsonSize;
};

const EmbeddedModelAsset* GetAmpModelAsset(const char* token);
const EmbeddedModelAsset* GetStompModelAsset(const char* token);
} // namespace embedded_model
