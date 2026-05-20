#include "runtime_test_environment.h"

namespace
{
class RepositoryWorkingDirectory
{
  public:
    RepositoryWorkingDirectory()
    {
        std::filesystem::current_path(yaaf::tests::repository_root());
    }
};

const RepositoryWorkingDirectory kRepositoryWorkingDirectory;
} // namespace
