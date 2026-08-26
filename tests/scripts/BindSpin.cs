public class BindSpin : Script
{
    public override void OnUpdate(float deltaTime)
    {
        Transform.Rotation.Y += deltaTime;
    }
}
