public class Spin : Script
{
    public float Angle;

    public override void OnUpdate(float deltaTime)
    {
        Angle += 90.0f * deltaTime;
        Console.WriteLine($"Spin.OnUpdate dt={deltaTime:0.000} angle={Angle:0.0}");
    }
}
