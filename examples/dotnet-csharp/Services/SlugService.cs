using System.Text;
using System.Text.RegularExpressions;

namespace RealWorld.Services;

public interface ISlugService
{
    string GenerateSlug(string title);
}

public class SlugService : ISlugService
{
    public string GenerateSlug(string title)
    {
        var slug = title.ToLowerInvariant();
        slug = Regex.Replace(slug, @"[^a-z0-9\s-]", "");
        slug = Regex.Replace(slug, @"\s+", "-");
        slug = Regex.Replace(slug, @"-+", "-");
        slug = slug.Trim('-');
        
        var randomChars = Guid.NewGuid().ToString("N").Substring(0, 6);
        return $"{slug}-{randomChars}";
    }
}
