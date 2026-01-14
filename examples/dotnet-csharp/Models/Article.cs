namespace RealWorld.Models;

public class Article
{
    public int Id { get; set; }
    public required string Slug { get; set; }
    public required string Title { get; set; }
    public required string Description { get; set; }
    public required string Body { get; set; }
    public DateTime CreatedAt { get; set; }
    public DateTime UpdatedAt { get; set; }
    
    public int AuthorId { get; set; }
    public User Author { get; set; } = null!;
    
    public ICollection<Tag> Tags { get; set; } = new List<Tag>();
    public ICollection<Comment> Comments { get; set; } = new List<Comment>();
    public ICollection<Favorite> Favorites { get; set; } = new List<Favorite>();
}
