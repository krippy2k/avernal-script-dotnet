using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;

namespace Avernal.Scripting;

public static class NativeHost
{
    const string TargetFramework = "net8.0";

    static readonly Dictionary<string, Type> Types = new(StringComparer.Ordinal);
    static readonly HashSet<string> LoadedAssemblies = new(StringComparer.OrdinalIgnoreCase);
    static readonly AssemblyLoadContext LoadContext =
        AssemblyLoadContext.GetLoadContext(typeof(Script).Assembly) ?? AssemblyLoadContext.Default;

    static NativeHost()
    {
        LoadContext.Resolving += (_, name) =>
            name.Name == typeof(Script).Assembly.GetName().Name ? typeof(Script).Assembly : null;
    }

    [UnmanagedCallersOnly]
    public static int Load(nint pathUtf8)
    {
        try
        {
            var path = Marshal.PtrToStringUTF8(pathUtf8);
            return !string.IsNullOrWhiteSpace(path) && LoadPath(path) ? 1 : 0;
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static int HasType(nint nameUtf8)
    {
        try
        {
            var name = Marshal.PtrToStringUTF8(nameUtf8);
            return name is not null && Types.ContainsKey(name) ? 1 : 0;
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static nint Create(nint nameUtf8)
    {
        try
        {
            var name = Marshal.PtrToStringUTF8(nameUtf8);
            if (name is null || !Types.TryGetValue(name, out var type))
            {
                return 0;
            }

            var instance = Activator.CreateInstance(type) as Script;
            return instance is null ? 0 : GCHandle.ToIntPtr(GCHandle.Alloc(instance));
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static void OnCreate(nint handle)
    {
        try
        {
            ScriptOf(handle).OnCreate();
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    public static void OnUpdate(nint handle, float deltaTime)
    {
        try
        {
            ScriptOf(handle).OnUpdate(deltaTime);
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    public static void OnDestroy(nint handle)
    {
        try
        {
            ScriptOf(handle).OnDestroy();
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    public static void Release(nint handle)
    {
        try
        {
            GCHandle.FromIntPtr(handle).Free();
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    public static void BindUserData(nint handle, nint userData)
    {
        try
        {
            ScriptOf(handle).BindUserData(userData);
        }
        catch
        {
        }
    }

    static Script ScriptOf(nint handle)
    {
        return (Script)GCHandle.FromIntPtr(handle).Target!;
    }

    static bool LoadPath(string path)
    {
        path = Path.GetFullPath(path);
        if (File.Exists(path) && path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
        {
            return LoadAssembly(path);
        }

        if (File.Exists(path) && path.EndsWith(".cs", StringComparison.OrdinalIgnoreCase))
        {
            return CompileAndLoad([path]);
        }

        if (Directory.Exists(path))
        {
            var sources = Directory.GetFiles(path, "*.cs");
            return sources.Length > 0 && CompileAndLoad(sources);
        }

        return false;
    }

    static bool LoadAssembly(string path)
    {
        path = Path.GetFullPath(path);
        if (!LoadedAssemblies.Add(path))
        {
            return true;
        }

        var assembly = LoadContext.Assemblies.FirstOrDefault(loaded =>
            string.Equals(loaded.Location, path, StringComparison.OrdinalIgnoreCase));
        assembly ??= LoadContext.LoadFromAssemblyPath(path);
        RegisterTypes(assembly);
        return true;
    }

    static void RegisterTypes(Assembly assembly)
    {
        foreach (var type in assembly.GetExportedTypes())
        {
            if (type.IsAbstract || !type.IsClass || !type.IsSubclassOf(typeof(Script)))
            {
                continue;
            }

            Types[type.Name] = type;
            if (type.FullName is { } fullName)
            {
                Types[fullName] = type;
            }
        }
    }

    static bool CompileAndLoad(string[] sources)
    {
        Array.Sort(sources, StringComparer.OrdinalIgnoreCase);
        var hash = HashSources(sources);
        var work = Path.Combine(Path.GetTempPath(), "avernal-script", hash);
        var output = Path.Combine(work, "out");
        var dll = Path.Combine(output, $"Avernal.Scripts.{hash}.dll");
        if (File.Exists(dll) &&
            sources.All(source => File.GetLastWriteTimeUtc(source) <= File.GetLastWriteTimeUtc(dll)))
        {
            return LoadAssembly(dll);
        }

        Directory.CreateDirectory(work);
        Directory.CreateDirectory(output);

        var scripting = typeof(Script).Assembly.Location;
        var csproj = Path.Combine(work, "Avernal.Scripts.csproj");
        File.WriteAllText(csproj, $$"""
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <TargetFramework>{{TargetFramework}}</TargetFramework>
                <ImplicitUsings>enable</ImplicitUsings>
                <Nullable>enable</Nullable>
                <EnableDynamicLoading>true</EnableDynamicLoading>
                <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
                <AssemblyName>Avernal.Scripts.{{hash}}</AssemblyName>
              </PropertyGroup>
              <ItemGroup>
                <Using Include="Avernal.Scripting" />
                <Reference Include="Avernal.Scripting">
                  <HintPath>{{XmlEscape(scripting)}}</HintPath>
                  <Private>false</Private>
                </Reference>
              </ItemGroup>
              <ItemGroup>
            {{string.Join(Environment.NewLine, sources.Select(source => $"    <Compile Include=\"{XmlEscape(source)}\" />"))}}
              </ItemGroup>
            </Project>
            """);

        var start = new ProcessStartInfo(FindDotnet(), $"build \"{csproj}\" -o \"{output}\" --nologo -v q")
        {
            WorkingDirectory = work,
            CreateNoWindow = true,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };

        using var process = Process.Start(start);
        if (process is null)
        {
            return false;
        }

        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(120_000))
        {
            process.Kill(entireProcessTree: true);
            return false;
        }

        if (process.ExitCode != 0)
        {
            Console.Error.Write(stdout.GetAwaiter().GetResult());
            Console.Error.Write(stderr.GetAwaiter().GetResult());
            return false;
        }

        return File.Exists(dll) && LoadAssembly(dll);
    }

    static string HashSources(string[] sources)
    {
        using var stream = new MemoryStream();
        foreach (var source in sources)
        {
            stream.Write(File.ReadAllBytes(source));
        }

        stream.Write(Encoding.UTF8.GetBytes(typeof(Script).Assembly.Location));
        return Convert.ToHexString(SHA256.HashData(stream.ToArray()))[..16];
    }

    static string FindDotnet()
    {
        var root = Environment.GetEnvironmentVariable("DOTNET_ROOT");
        if (!string.IsNullOrEmpty(root))
        {
            var bundled = Path.Combine(root, OperatingSystem.IsWindows() ? "dotnet.exe" : "dotnet");
            if (File.Exists(bundled))
            {
                return bundled;
            }
        }

        var programFiles = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            "dotnet",
            "dotnet.exe");
        return File.Exists(programFiles) ? programFiles : "dotnet";
    }

    static string XmlEscape(string value)
    {
        return value.Replace("&", "&amp;", StringComparison.Ordinal)
            .Replace("\"", "&quot;", StringComparison.Ordinal)
            .Replace("<", "&lt;", StringComparison.Ordinal);
    }
}
