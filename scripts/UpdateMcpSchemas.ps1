param(
    [string]$Repository = "modelcontextprotocol/modelcontextprotocol",
    [string]$Ref = "main"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $repoRoot "libyaaf/mcp/mcp_schema_generated.h"
$sourcePath = Join-Path $repoRoot "libyaaf/mcp/mcp_schema_generated.cpp"
$schemaSourceRoot = Join-Path $repoRoot "libyaaf/mcp/schema"
$schemaOutputRoot = Join-Path $repoRoot "third_party/mcp/schema"

function Get-Json($Uri) {
    return Invoke-WebRequest -Uri $Uri -UseBasicParsing | ConvertFrom-Json
}

function Get-Text($Uri) {
    return (Invoke-WebRequest -Uri $Uri -UseBasicParsing).Content
}

function Escape-CppString($Value) {
    return $Value.Replace("\", "\\").Replace('"', '\"')
}

function Write-GeneratedFile($Path, $Content) {
    $directory = Split-Path -Parent $Path
    if ($directory) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    Set-Content -Path $Path -Value $Content -Encoding utf8
}

function Get-CppIdentifier($Value) {
    return $Value.Replace("-", "_")
}

$schemaRoot = "https://api.github.com/repos/$Repository/contents/schema?ref=$Ref"
$versions = Get-Json $schemaRoot |
    Where-Object { $_.type -eq "dir" -and $_.name -match '^\d{4}-\d{2}-\d{2}$' } |
    Sort-Object name

if (-not $versions) {
    throw "No versioned MCP schemas were found at $schemaRoot"
}

$versionInfos = @()

foreach ($version in $versions) {
    $schemaUrl = "https://raw.githubusercontent.com/$Repository/$Ref/schema/$($version.name)/schema.json"
    $schemaText = Get-Text $schemaUrl
    $schema = $schemaText | ConvertFrom-Json

    $defs = $schema.'$defs'
    if ($null -eq $defs) {
        $defs = $schema.definitions
    }
    if ($null -eq $defs) {
        throw "Schema $schemaUrl does not contain a `$defs or definitions object"
    }

    $schemaPath = Join-Path $schemaOutputRoot "$($version.name)/schema.json"
    Write-GeneratedFile $schemaPath $schemaText

    $definitionNames = [System.Collections.Generic.List[string]]::new()
    $methods = [System.Collections.Generic.List[object]]::new()

    foreach ($definition in ($defs.PSObject.Properties | Sort-Object Name)) {
        [void]$definitionNames.Add($definition.Name)

        $properties = $definition.Value.properties
        if ($null -eq $properties -or $null -eq $properties.method) {
            continue
        }

        $methodName = $properties.method.const
        if ([string]::IsNullOrWhiteSpace($methodName)) {
            continue
        }

        $kind = if ($definition.Name.EndsWith("Notification")) { "notification" } else { "request" }
        [void]$methods.Add([pscustomobject]@{
            Method = $methodName
            Definition = $definition.Name
            Kind = $kind
        })
    }

    $methods = $methods | Sort-Object Method, Definition

    $versionInfos += [pscustomobject]@{
        Version = $version.name
        SchemaUrl = $schemaUrl
        SchemaPath = "third_party/mcp/schema/$($version.name)/schema.json"
        Definitions = $definitionNames
        Methods = $methods
    }
}

$header = @'
#pragma once

#include "mcp_schema.h"

namespace yaaf::mcp::schema
{
class GeneratedBackendFactory final : public BackendFactory
{
  public:
    [[nodiscard]] std::shared_ptr<const Backend> create(std::string_view version) const override;
    [[nodiscard]] std::shared_ptr<const Backend> create_latest() const override;
    [[nodiscard]] std::shared_ptr<const Registry> create_registry() const override;
};

[[nodiscard]] std::shared_ptr<const BackendFactory> generated_factory();

// One declaration is generated for each versioned schema source in mcp/schema/.
// The generated registry uses these functions to avoid centralizing all schema data in one translation unit.
__BACKEND_DECLARATIONS__
} // namespace yaaf::mcp::schema
'@

if (Test-Path $schemaSourceRoot) {
    Get-ChildItem -Path $schemaSourceRoot -Filter "schema-*.cpp" | Remove-Item -Force
}
else {
    New-Item -ItemType Directory -Path $schemaSourceRoot -Force | Out-Null
}

$versionRows = $versionInfos | ForEach-Object {
    '    {{"{0}", "{1}", "{2}", {3}, {4}}},' -f (Escape-CppString $_.Version), (Escape-CppString $_.SchemaUrl), (Escape-CppString $_.SchemaPath), $_.Definitions.Count, $_.Methods.Count
}

$backendDeclarations = @()
$registryRows = @()
$factoryRows = @()
$generatedSchemaFiles = @()
$versionIndex = 0
foreach ($versionInfo in $versionInfos) {
    $identifier = Get-CppIdentifier $versionInfo.Version
    $className = "SchemaBackend$identifier"
    $functionName = "generated_backend_$identifier"
    $schemaSourceFile = "schema-$($versionInfo.Version).cpp"
    $definitionRows = @($versionInfo.Definitions) | ForEach-Object {
        '    "{0}",' -f (Escape-CppString $_)
    }
    $methodRows = @($versionInfo.Methods) | ForEach-Object {
        '    {{"{0}", "{1}", MessageKind::{2}}},' -f (Escape-CppString $_.Method), (Escape-CppString $_.Definition), $_.Kind
    }

    $backendDeclarations += "[[nodiscard]] std::shared_ptr<const Backend> $functionName();"
    $registryRows += "              $functionName(),"
    $factoryRows += @"
    if (version == "$($versionInfo.Version)")
    {
        return $functionName();
    }
"@
    $generatedSchemaFiles += $schemaSourceFile

    $schemaSource = @"
#include "../mcp_schema_generated.h"

namespace yaaf::mcp::schema
{
namespace
{
const VersionInfo kVersionInfo = {"$($versionInfo.Version)", "$(Escape-CppString $versionInfo.SchemaUrl)", "$(Escape-CppString $versionInfo.SchemaPath)", $($versionInfo.Definitions.Count), $($versionInfo.Methods.Count)};

const std::vector<std::string_view> kDefinitions = {
$($definitionRows -join "`n")
};

const std::vector<MethodInfo> kMethods = {
$($methodRows -join "`n")
};

class $className final : public Backend
{
  public:
    [[nodiscard]] const VersionInfo &info() const override
    {
        return kVersionInfo;
    }

    [[nodiscard]] const std::vector<MethodInfo> &methods() const override
    {
        return kMethods;
    }

    [[nodiscard]] const std::vector<std::string_view> &definitions() const override
    {
        return kDefinitions;
    }

    [[nodiscard]] bool has_definition(std::string_view definition) const override
    {
        return std::find(kDefinitions.begin(), kDefinitions.end(), definition) != kDefinitions.end();
    }

    [[nodiscard]] std::optional<MethodInfo> method(std::string_view method) const override
    {
        const auto found = std::find_if(kMethods.begin(), kMethods.end(),
                                        [method](const MethodInfo &entry) { return entry.method == method; });
        if (found == kMethods.end())
        {
            return std::nullopt;
        }
        return *found;
    }
};
} // namespace

std::shared_ptr<const Backend> $functionName()
{
    static const auto instance = std::make_shared<$className>();
    return instance;
}
} // namespace yaaf::mcp::schema
"@

    Write-GeneratedFile (Join-Path $schemaSourceRoot $schemaSourceFile) $schemaSource
    $versionIndex += 1
}

$header = $header.Replace("__BACKEND_DECLARATIONS__", ($backendDeclarations -join "`n"))

$latestVersion = @($versionInfos)[@($versionInfos).Count - 1]
$latestMethodNames = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($methodInfo in @($latestVersion.Methods)) {
    [void]$latestMethodNames.Add($methodInfo.Method)
}
$knownMethodRows = $latestMethodNames | ForEach-Object {
    '    "{0}",' -f (Escape-CppString $_)
}

$source = @"
#include "mcp_schema_generated.h"

namespace yaaf::mcp::schema
{
namespace
{
const std::vector<VersionInfo> kSupportedVersions = {
$($versionRows -join "`n")
};

const std::vector<std::string_view> kKnownMethods = {
$($knownMethodRows -join "`n")
};

const std::vector<MethodInfo> kEmptyMethods;
const std::vector<std::string_view> kEmptyDefinitions;

class GeneratedRegistry final : public Registry
{
  public:
    GeneratedRegistry()
        : backends_{
$($registryRows -join "`n")
          }
    {
    }

    [[nodiscard]] std::string_view latest_protocol_version() const override
    {
        return kSupportedVersions.back().version;
    }

    [[nodiscard]] const std::vector<VersionInfo> &supported_versions() const override
    {
        return kSupportedVersions;
    }

    [[nodiscard]] std::shared_ptr<const Backend> backend(std::string_view version) const override
    {
        const auto found = std::find_if(backends_.begin(), backends_.end(), [version](const auto &entry) {
            return entry->info().version == version;
        });
        return found == backends_.end() ? nullptr : *found;
    }

    [[nodiscard]] bool is_supported_protocol_version(std::string_view version) const override
    {
        return backend(version) != nullptr;
    }

  private:
    std::vector<std::shared_ptr<const Backend>> backends_;
};
} // namespace

std::shared_ptr<const Backend> GeneratedBackendFactory::create(std::string_view version) const
{
$($factoryRows -join "`n")
    return nullptr;
}

std::shared_ptr<const Backend> GeneratedBackendFactory::create_latest() const
{
    return create(kSupportedVersions.back().version);
}

std::shared_ptr<const Registry> GeneratedBackendFactory::create_registry() const
{
    return std::make_shared<GeneratedRegistry>();
}

std::shared_ptr<const BackendFactory> generated_factory()
{
    static const auto instance = std::make_shared<GeneratedBackendFactory>();
    return instance;
}

std::shared_ptr<const BackendFactory> default_factory()
{
    return generated_factory();
}

const std::shared_ptr<const Registry> &default_registry()
{
    static const auto instance = generated_factory()->create_registry();
    return instance;
}

std::string_view latest_protocol_version()
{
    return default_registry()->latest_protocol_version();
}

const std::vector<VersionInfo> &supported_versions()
{
    return default_registry()->supported_versions();
}

const std::vector<std::string_view> &known_methods()
{
    return kKnownMethods;
}

const std::vector<MethodInfo> &methods(std::string_view version)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->methods();
    }
    return kEmptyMethods;
}

const std::vector<std::string_view> &definitions(std::string_view version)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->definitions();
    }
    return kEmptyDefinitions;
}

bool is_supported_protocol_version(std::string_view version)
{
    return default_registry()->is_supported_protocol_version(version);
}

bool has_definition(std::string_view version, std::string_view definition)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->has_definition(definition);
    }
    return false;
}

std::optional<MethodInfo> method(std::string_view version, std::string_view method)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->method(method);
    }
    return std::nullopt;
}
} // namespace yaaf::mcp::schema
"@

Write-GeneratedFile $headerPath $header
Write-GeneratedFile $sourcePath $source

Write-Host "Updated MCP schema metadata for $($versionInfos.Count) protocol versions, $($latestMethodNames.Count) latest-version methods, and $($generatedSchemaFiles.Count) generated schema source files."