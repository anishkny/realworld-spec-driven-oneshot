namespace RealWorld.Models;

public class Follow
{
    public int FollowerId { get; set; }
    public User Follower { get; set; } = null!;
    
    public int FollowingId { get; set; }
    public User Following { get; set; } = null!;
}
