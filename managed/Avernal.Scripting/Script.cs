using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Avernal.Scripting;

[StructLayout(LayoutKind.Sequential)]
public struct Vec3
{
    public float X;
    public float Y;
    public float Z;

    public Vec3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct Transform
{
    public Vec3 Position;
    public Vec3 Rotation;
    public Vec3 Scale;
}

public abstract class Script
{
    nint _userData;

    public virtual void OnCreate() {}
    public virtual void OnUpdate(float deltaTime) {}
    public virtual void OnDestroy() {}

    internal void BindUserData(nint pointer) => _userData = pointer;

    public unsafe ref Transform Transform
    {
        get
        {
            if (_userData == 0)
            {
                throw new InvalidOperationException("script has no bound transform");
            }

            return ref Unsafe.AsRef<Transform>((void*)_userData);
        }
    }
}
