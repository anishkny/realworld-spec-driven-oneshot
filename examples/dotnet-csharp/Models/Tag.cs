namespace RealWorld.Models;

public class Tag
{
    public int Id { get; set; }
    public required string Name { get; set; }
    
    public ICollection<Article> Articles { get; set; } = new List<Article>();
}
