public class Counter : Script
{
    public int Ticks;

    public override void OnUpdate(float deltaTime)
    {
        ++Ticks;
    }
}
