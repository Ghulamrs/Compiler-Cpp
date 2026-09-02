#!/usr/bin/env python3
"""Write the Xcode and Visual Studio projects for cxx1 from the source tree.

The Makefile finds its sources with a wildcard, so a project file listing them
by hand rots the first time somebody adds one. This writes both from what is on
disk now:

    ./generate.py            regenerate both projects
    ./generate.py --check    say whether they are up to date, and change nothing

Every flag here is the one the Makefile or msvc/build.cmd already uses; where
the two toolchains differ, the difference is commented at the line that makes it.
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))            # the checkout
NAME = "cxx1"
UP = ".."                    # from ide/ to the tree, in both project dialects


def sources():
    """Every .cpp the build compiles, in the Makefile's own order."""
    out = []
    for d in ("", "parser", "backend"):
        base = os.path.join(ROOT, "src", d)
        for f in sorted(os.listdir(base)):
            if f.endswith(".cpp") and " " not in f:      # macOS " 2.cpp" copies
                out.append(os.path.join("src", d, f).replace("\\", "/").replace("//", "/"))
    return out


def headers():
    out = []
    for d in ("", "parser", "backend"):
        base = os.path.join(ROOT, "src", d)
        for f in sorted(os.listdir(base)):
            if f.endswith(".h") and " " not in f:
                out.append(os.path.join("src", d, f).replace("\\", "/").replace("//", "/"))
    return out


def uid(text):
    """A stable 24-hex-digit id. Xcode only asks that they be unique and stable;
    deriving them from the path keeps a regenerated project diffable."""
    return hashlib.sha1(text.encode()).hexdigest()[:24].upper()


# ---------------------------------------------------------------- Xcode

def xcode(srcs, hdrs):
    """project.pbxproj for a command-line tool target.

    The flags are the Makefile's: -std=c++14 -O2 -g -Wall -Wextra -Werror
    -pedantic, and CXX1_INCLUDE_DIR pointing at the checkout's lib/, which the
    driver compiles into the binary as an absolute path.
    """
    files, builds, groups = [], [], {}
    for p in srcs + hdrs:
        fid, bid = uid("f:" + p), uid("b:" + p)
        kind = "sourcecode.cpp.cpp" if p.endswith(".cpp") else "sourcecode.c.h"
        files.append('\t\t%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = %s; '
                     'name = %s; path = "%s"; sourceTree = "<group>"; };'
                     % (fid, os.path.basename(p), kind, os.path.basename(p),
                        UP + "/" + p))
        if p.endswith(".cpp"):
            builds.append('\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; fileRef = %s /* %s */; };'
                          % (bid, os.path.basename(p), fid, os.path.basename(p)))
        groups.setdefault(os.path.dirname(p), []).append((fid, os.path.basename(p)))

    group_secs, group_children = [], []
    for d in sorted(groups):
        gid = uid("g:" + d)
        kids = "\n".join('\t\t\t\t%s /* %s */,' % (f, n) for f, n in sorted(groups[d], key=lambda x: x[1]))
        group_secs.append('\t\t%s /* %s */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n%s\n\t\t\t);\n'
                          '\t\t\tname = %s;\n\t\t\tsourceTree = "<group>";\n\t\t};'
                          % (gid, d or "src", kids, '"%s"' % (d or "src")))
        group_children.append('\t\t\t\t%s /* %s */,' % (gid, d or "src"))

    src_phase = "\n".join('\t\t\t\t%s /* %s in Sources */,' % (uid("b:" + p), os.path.basename(p))
                          for p in srcs)
    inc = os.path.join(ROOT, "lib")
    common = ('\t\t\t\tALWAYS_SEARCH_USER_PATHS = NO;\n'
              # **The Makefile's warning line and no other.** Xcode's template
              # adds -Wshorten-64-to-32, which -Wall -Wextra do not, and it fires
              # on the bitfield arithmetic that is deliberately done in long long
              # and narrowed after a check. A project that builds this tree with
              # a different warning set is a fourth opinion nothing else gates on.
              '\t\t\t\tGCC_WARN_64_TO_32_BIT_CONVERSION = NO;\n'
              # One architecture, as `make` builds: the target is a run-time
              # choice here, so a universal binary buys nothing but time.
              '\t\t\t\tONLY_ACTIVE_ARCH = YES;\n'
              '\t\t\t\tCLANG_CXX_LANGUAGE_STANDARD = "c++14";\n'
              '\t\t\t\tCLANG_CXX_LIBRARY = "libc++";\n'
              '\t\t\t\tCLANG_ENABLE_OBJC_ARC = YES;\n'
              '\t\t\t\tCODE_SIGN_STYLE = Automatic;\n'
              # the Makefile's own warning set, and -Werror with it
              '\t\t\t\tGCC_TREAT_WARNINGS_AS_ERRORS = YES;\n'
              '\t\t\t\tWARNING_CFLAGS = (\n\t\t\t\t\t"-Wall",\n\t\t\t\t\t"-Wextra",\n'
              '\t\t\t\t\t"-pedantic",\n\t\t\t\t);\n'
              # the include directory the driver bakes in, as the Makefile does
              '\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = (\n'
              '\t\t\t\t\t"CXX1_INCLUDE_DIR=\\\\\\"%s\\\\\\"",\n\t\t\t\t);\n'
              '\t\t\t\tPRODUCT_NAME = "%s";\n'
              '\t\t\t\tHEADER_SEARCH_PATHS = "%s/src";\n' % (inc, NAME, ROOT))
    return files, builds, group_secs, group_children, src_phase, common


def write_xcode(srcs, hdrs, check):
    files, builds, group_secs, group_children, src_phase, common = xcode(srcs, hdrs)
    proj = os.path.join(HERE, NAME + ".xcodeproj")
    pb = os.path.join(proj, "project.pbxproj")

    ids = {k: uid(k) for k in ("project", "target", "product", "productgroup",
                               "mainGroup", "sources", "cfgProject", "cfgTarget",
                               "dbgP", "relP", "dbgT", "relT")}
    text = f"""// !$*UTF8*$!
{{
	archiveVersion = 1;
	classes = {{}};
	objectVersion = 54;
	objects = {{

/* Begin PBXBuildFile section */
{chr(10).join(builds)}
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
{chr(10).join(files)}
		{ids['product']} /* {NAME} */ = {{isa = PBXFileReference; explicitFileType = "compiled.mach-o.executable"; includeInIndex = 0; path = {NAME}; sourceTree = BUILT_PRODUCTS_DIR; }};
/* End PBXFileReference section */

/* Begin PBXGroup section */
		{ids['mainGroup']} = {{
			isa = PBXGroup;
			children = (
{chr(10).join(group_children)}
				{ids['productgroup']} /* Products */,
			);
			sourceTree = "<group>";
		}};
		{ids['productgroup']} /* Products */ = {{
			isa = PBXGroup;
			children = (
				{ids['product']} /* {NAME} */,
			);
			name = Products;
			sourceTree = "<group>";
		}};
{chr(10).join(group_secs)}
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
		{ids['target']} /* {NAME} */ = {{
			isa = PBXNativeTarget;
			buildConfigurationList = {ids['cfgTarget']};
			buildPhases = (
				{ids['sources']} /* Sources */,
			);
			dependencies = ();
			name = {NAME};
			productName = {NAME};
			productReference = {ids['product']} /* {NAME} */;
			productType = "com.apple.product-type.tool";
		}};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		{ids['project']} /* Project object */ = {{
			isa = PBXProject;
			attributes = {{
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 2600;
			}};
			buildConfigurationList = {ids['cfgProject']};
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (en, Base);
			mainGroup = {ids['mainGroup']};
			productRefGroup = {ids['productgroup']} /* Products */;
			projectDirPath = "";
			projectRoot = "";
			targets = (
				{ids['target']} /* {NAME} */,
			);
		}};
/* End PBXProject section */

/* Begin PBXSourcesBuildPhase section */
		{ids['sources']} /* Sources */ = {{
			isa = PBXSourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
{src_phase}
			);
			runOnlyForDeploymentPostprocessing = 0;
		}};
/* End PBXSourcesBuildPhase section */

/* Begin XCBuildConfiguration section */
		{ids['dbgP']} /* Debug */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
{common}				GCC_OPTIMIZATION_LEVEL = 0;
			}};
			name = Debug;
		}};
		{ids['relP']} /* Release */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
{common}				GCC_OPTIMIZATION_LEVEL = 2;
			}};
			name = Release;
		}};
		{ids['dbgT']} /* Debug */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
				PRODUCT_NAME = "{NAME}";
			}};
			name = Debug;
		}};
		{ids['relT']} /* Release */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
				PRODUCT_NAME = "{NAME}";
			}};
			name = Release;
		}};
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
		{ids['cfgProject']} = {{
			isa = XCConfigurationList;
			buildConfigurations = (
				{ids['dbgP']} /* Debug */,
				{ids['relP']} /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		}};
		{ids['cfgTarget']} = {{
			isa = XCConfigurationList;
			buildConfigurations = (
				{ids['dbgT']} /* Debug */,
				{ids['relT']} /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		}};
/* End XCConfigurationList section */
	}};
	rootObject = {ids['project']} /* Project object */;
}}
"""
    ws = os.path.join(HERE, NAME + ".xcworkspace")
    wsdata = ('<?xml version="1.0" encoding="UTF-8"?>\n<Workspace version = "1.0">\n'
              '   <FileRef location = "group:%s.xcodeproj"></FileRef>\n</Workspace>\n' % NAME)

    # **A shared scheme, because an implicit one is not a file.** Xcode makes one
    # when a person opens the project and keeps it under xcuserdata, where it is
    # nobody else's; xcodebuild -scheme and any CI want one that is checked in.
    scheme = f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "2600" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = "YES" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES">
            <BuildableReference
               BuildableIdentifier = "primary"
               BlueprintIdentifier = "{ids['target']}"
               BuildableName = "{NAME}"
               BlueprintName = "{NAME}"
               ReferencedContainer = "container:{NAME}.xcodeproj">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <LaunchAction buildConfiguration = "Release" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" debugServiceExtension = "internal" allowLocationSimulation = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
         <BuildableReference
            BuildableIdentifier = "primary"
            BlueprintIdentifier = "{ids['target']}"
            BuildableName = "{NAME}"
            BlueprintName = "{NAME}"
            ReferencedContainer = "container:{NAME}.xcodeproj">
         </BuildableReference>
      </BuildableProductRunnable>
   </LaunchAction>
   <AnalyzeAction buildConfiguration = "Release"></AnalyzeAction>
   <ArchiveAction buildConfiguration = "Release" revealArchiveInOrganizer = "YES"></ArchiveAction>
</Scheme>
"""
    schemedir = os.path.join(proj, "xcshareddata", "xcschemes")
    spath = os.path.join(schemedir, NAME + ".xcscheme")

    if check:
        old = open(pb).read() if os.path.exists(pb) else ""
        oldscheme = open(spath).read() if os.path.exists(spath) else ""
        return old == text and oldscheme == scheme
    os.makedirs(proj, exist_ok=True)
    os.makedirs(ws, exist_ok=True)
    os.makedirs(schemedir, exist_ok=True)
    open(pb, "w").write(text)
    open(spath, "w").write(scheme)
    open(os.path.join(ws, "contents.xcworkspacedata"), "w").write(wsdata)
    return True


# ------------------------------------------------------- Visual Studio 2022

def write_vs(srcs, hdrs, check):
    """cxx1.vcxproj and cxx1.sln, carrying msvc/build.cmd's flags exactly.

    /std:c++14 and /permissive- are the pin the other two toolchains spell
    -std=c++14; /W4 /WX with five warnings disabled is build.cmd's list, and
    every one of them fires on code this tree forked rather than wrote.
    """
    def win(p):
        return "..\\" + p.replace("/", "\\")

    cl = "\n".join('    <ClCompile Include="%s" />' % win(p) for p in srcs)
    hd = "\n".join('    <ClInclude Include="%s" />' % win(p) for p in hdrs)
    guid = "{" + uid("vs:" + NAME)[:8] + "-" + uid("vs:g1")[:4] + "-" + \
           uid("vs:g2")[:4] + "-" + uid("vs:g3")[:4] + "-" + uid("vs:g4")[:12] + "}"

    # The include directory is compiled into the binary as a C string literal,
    # so it must be spelled with forward slashes: a backslash there starts an
    # escape and the error lands in Driver.cpp, which is not the file at fault.
    incdir = "$([System.String]::Copy('$(ProjectDir)..\\lib').Replace('\\','/'))"

    proj = f"""<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{guid}</ProjectGuid>
    <RootNamespace>{NAME}</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <UseDebugLibraries>false</UseDebugLibraries>
    <WholeProgramOptimization>false</WholeProgramOptimization>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)build\\$(Configuration)\\</OutDir>
    <IntDir>$(ProjectDir)build\\$(Configuration)\\obj\\</IntDir>
    <TargetName>{NAME}</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpp14</LanguageStandard>
      <ConformanceMode>true</ConformanceMode>
      <ExceptionHandling>Sync</ExceptionHandling>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <DisableSpecificWarnings>4996;4267;4244;4456;4146</DisableSpecificWarnings>
      <AdditionalIncludeDirectories>$(ProjectDir)..\\msvc\\compat;$(ProjectDir)..\\src;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;CXX1_INCLUDE_DIR="{incdir}";%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
    </ClCompile>
    <Link><SubSystem>Console</SubSystem></Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile><Optimization>MaxSpeed</Optimization></ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile><Optimization>Disabled</Optimization></ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
{cl}
  </ItemGroup>
  <ItemGroup>
{hd}
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
"""
    sln = f"""Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "{NAME}", "{NAME}.vcxproj", "{guid}"
EndProject
Global
\tGlobalSection(SolutionConfigurationPlatforms) = preSolution
\t\tDebug|x64 = Debug|x64
\t\tRelease|x64 = Release|x64
\tEndGlobalSection
\tGlobalSection(ProjectConfigurationPlatforms) = postSolution
\t\t{guid}.Debug|x64.ActiveCfg = Debug|x64
\t\t{guid}.Debug|x64.Build.0 = Debug|x64
\t\t{guid}.Release|x64.ActiveCfg = Release|x64
\t\t{guid}.Release|x64.Build.0 = Release|x64
\tEndGlobalSection
EndGlobal
"""
    pp, sp = os.path.join(HERE, NAME + ".vcxproj"), os.path.join(HERE, NAME + ".sln")
    if check:
        ok = os.path.exists(pp) and open(pp).read() == proj
        return ok and os.path.exists(sp) and open(sp).read() == sln
    open(pp, "w", newline="\r\n").write(proj)
    open(sp, "w", newline="\r\n").write(sln)
    return True


if __name__ == "__main__":
    check = "--check" in sys.argv
    s, h = sources(), headers()
    x, v = write_xcode(s, h, check), write_vs(s, h, check)
    if check:
        print("up to date" if (x and v) else "STALE - run ./generate.py")
        sys.exit(0 if (x and v) else 1)
    print("wrote %s.xcodeproj, %s.xcworkspace, %s.vcxproj and %s.sln for %d sources"
          % (NAME, NAME, NAME, NAME, len(s)))
