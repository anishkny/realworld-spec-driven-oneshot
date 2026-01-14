using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using RealWorld.Data;
using RealWorld.DTOs;
using RealWorld.Models;
using RealWorld.Services;

namespace RealWorld.Controllers;

[ApiController]
[Route("api/articles")]
public class ArticlesController : ControllerBase
{
    private readonly ApplicationDbContext _context;
    private readonly IJwtService _jwtService;
    private readonly ISlugService _slugService;

    public ArticlesController(ApplicationDbContext context, IJwtService jwtService, ISlugService slugService)
    {
        _context = context;
        _jwtService = jwtService;
        _slugService = slugService;
    }

    [HttpGet]
    public async Task<IActionResult> ListArticles(
        [FromQuery] string? tag,
        [FromQuery] string? author,
        [FromQuery] string? favorited,
        [FromQuery] int limit = 20,
        [FromQuery] int offset = 0)
    {
        var query = _context.Articles
            .Include(a => a.Author)
            .Include(a => a.Tags)
            .Include(a => a.Favorites)
            .AsQueryable();

        if (!string.IsNullOrEmpty(tag))
        {
            query = query.Where(a => a.Tags.Any(t => t.Name == tag));
        }

        if (!string.IsNullOrEmpty(author))
        {
            query = query.Where(a => a.Author.Username == author);
        }

        if (!string.IsNullOrEmpty(favorited))
        {
            query = query.Where(a => a.Favorites.Any(f => f.User.Username == favorited));
        }

        var totalCount = await query.CountAsync();
        
        var articles = await query
            .OrderByDescending(a => a.CreatedAt)
            .Skip(offset)
            .Take(limit)
            .ToListAsync();

        var currentUserId = GetCurrentUserId();
        var articleDtos = await Task.WhenAll(articles.Select(a => MapToArticleDto(a, currentUserId)));

        return Ok(new ArticlesResponse(articleDtos, totalCount));
    }

    [HttpGet("feed")]
    public async Task<IActionResult> GetFeed([FromQuery] int limit = 20, [FromQuery] int offset = 0)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var followingIds = await _context.Follows
            .Where(f => f.FollowerId == currentUserId)
            .Select(f => f.FollowingId)
            .ToListAsync();

        var articles = await _context.Articles
            .Include(a => a.Author)
            .Include(a => a.Tags)
            .Include(a => a.Favorites)
            .Where(a => followingIds.Contains(a.AuthorId))
            .OrderByDescending(a => a.CreatedAt)
            .Skip(offset)
            .Take(limit)
            .ToListAsync();

        var totalCount = await _context.Articles
            .Where(a => followingIds.Contains(a.AuthorId))
            .CountAsync();

        var articleDtos = await Task.WhenAll(articles.Select(a => MapToArticleDto(a, currentUserId)));

        return Ok(new ArticlesResponse(articleDtos, totalCount));
    }

    [HttpGet("{slug}")]
    public async Task<IActionResult> GetArticle(string slug)
    {
        var article = await _context.Articles
            .Include(a => a.Author)
            .Include(a => a.Tags)
            .Include(a => a.Favorites)
            .FirstOrDefaultAsync(a => a.Slug == slug);

        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        var currentUserId = GetCurrentUserId();
        var articleDto = await MapToArticleDto(article, currentUserId);

        return Ok(new ArticleResponse(articleDto));
    }

    [HttpPost]
    public async Task<IActionResult> CreateArticle([FromBody] ArticleCreateRequestWrapper request)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var articleRequest = request.Article;

        if (string.IsNullOrWhiteSpace(articleRequest.Title))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "title can't be empty" } }));

        if (string.IsNullOrWhiteSpace(articleRequest.Description))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "description can't be empty" } }));

        if (string.IsNullOrWhiteSpace(articleRequest.Body))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "body can't be empty" } }));

        var slug = _slugService.GenerateSlug(articleRequest.Title);
        var now = DateTime.UtcNow;

        var article = new Article
        {
            Slug = slug,
            Title = articleRequest.Title,
            Description = articleRequest.Description,
            Body = articleRequest.Body,
            AuthorId = currentUserId.Value,
            CreatedAt = now,
            UpdatedAt = now
        };

        if (articleRequest.TagList != null && articleRequest.TagList.Length > 0)
        {
            foreach (var tagName in articleRequest.TagList)
            {
                var tag = await _context.Tags.FirstOrDefaultAsync(t => t.Name == tagName);
                if (tag == null)
                {
                    tag = new Tag { Name = tagName };
                    _context.Tags.Add(tag);
                }
                article.Tags.Add(tag);
            }
        }

        _context.Articles.Add(article);
        await _context.SaveChangesAsync();

        await _context.Entry(article).Reference(a => a.Author).LoadAsync();
        await _context.Entry(article).Collection(a => a.Tags).LoadAsync();
        await _context.Entry(article).Collection(a => a.Favorites).LoadAsync();

        var articleDto = await MapToArticleDto(article, currentUserId);
        return Ok(new ArticleResponse(articleDto));
    }

    [HttpPut("{slug}")]
    public async Task<IActionResult> UpdateArticle(string slug, [FromBody] ArticleUpdateRequestWrapper request)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var article = await _context.Articles
            .Include(a => a.Author)
            .Include(a => a.Tags)
            .Include(a => a.Favorites)
            .FirstOrDefaultAsync(a => a.Slug == slug);

        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        if (article.AuthorId != currentUserId)
            return StatusCode(403, new ErrorResponse(new() { ["body"] = new[] { "forbidden" } }));

        var update = request.Article;

        if (update.Title != null)
        {
            article.Title = update.Title;
            article.Slug = _slugService.GenerateSlug(update.Title);
        }

        if (update.Description != null)
            article.Description = update.Description;

        if (update.Body != null)
            article.Body = update.Body;

        article.UpdatedAt = DateTime.UtcNow;
        await _context.SaveChangesAsync();

        var articleDto = await MapToArticleDto(article, currentUserId);
        return Ok(new ArticleResponse(articleDto));
    }

    [HttpDelete("{slug}")]
    public async Task<IActionResult> DeleteArticle(string slug)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var article = await _context.Articles
            .FirstOrDefaultAsync(a => a.Slug == slug);

        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        if (article.AuthorId != currentUserId)
            return StatusCode(403, new ErrorResponse(new() { ["body"] = new[] { "forbidden" } }));

        _context.Articles.Remove(article);
        await _context.SaveChangesAsync();

        return Ok();
    }

    [HttpPost("{slug}/favorite")]
    public async Task<IActionResult> FavoriteArticle(string slug)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var article = await _context.Articles
            .Include(a => a.Author)
            .Include(a => a.Tags)
            .Include(a => a.Favorites)
            .FirstOrDefaultAsync(a => a.Slug == slug);

        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        var existingFavorite = await _context.Favorites
            .FirstOrDefaultAsync(f => f.UserId == currentUserId && f.ArticleId == article.Id);

        if (existingFavorite == null)
        {
            _context.Favorites.Add(new Favorite
            {
                UserId = currentUserId.Value,
                ArticleId = article.Id
            });
            await _context.SaveChangesAsync();
            await _context.Entry(article).Collection(a => a.Favorites).LoadAsync();
        }

        var articleDto = await MapToArticleDto(article, currentUserId);
        return Ok(new ArticleResponse(articleDto));
    }

    [HttpDelete("{slug}/favorite")]
    public async Task<IActionResult> UnfavoriteArticle(string slug)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var article = await _context.Articles
            .Include(a => a.Author)
            .Include(a => a.Tags)
            .Include(a => a.Favorites)
            .FirstOrDefaultAsync(a => a.Slug == slug);

        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        var favorite = await _context.Favorites
            .FirstOrDefaultAsync(f => f.UserId == currentUserId && f.ArticleId == article.Id);

        if (favorite != null)
        {
            _context.Favorites.Remove(favorite);
            await _context.SaveChangesAsync();
            await _context.Entry(article).Collection(a => a.Favorites).LoadAsync();
        }

        var articleDto = await MapToArticleDto(article, currentUserId);
        return Ok(new ArticleResponse(articleDto));
    }

    [HttpPost("{slug}/comments")]
    public async Task<IActionResult> AddComment(string slug, [FromBody] CommentCreateRequestWrapper request)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var article = await _context.Articles.FirstOrDefaultAsync(a => a.Slug == slug);
        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        var commentRequest = request.Comment;

        if (string.IsNullOrWhiteSpace(commentRequest.Body))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "body can't be empty" } }));

        var now = DateTime.UtcNow;
        var comment = new Comment
        {
            Body = commentRequest.Body,
            ArticleId = article.Id,
            AuthorId = currentUserId.Value,
            CreatedAt = now,
            UpdatedAt = now
        };

        _context.Comments.Add(comment);
        await _context.SaveChangesAsync();

        await _context.Entry(comment).Reference(c => c.Author).LoadAsync();

        var following = await _context.Follows
            .AnyAsync(f => f.FollowerId == currentUserId && f.FollowingId == comment.AuthorId);
        
        var author = new ProfileDto(comment.Author.Username, comment.Author.Bio, comment.Author.Image, following);
        var commentDto = new CommentDto(comment.Id, comment.CreatedAt, comment.UpdatedAt, comment.Body, author);
        
        return Ok(new CommentResponse(commentDto));
    }

    [HttpGet("{slug}/comments")]
    public async Task<IActionResult> GetComments(string slug)
    {
        var article = await _context.Articles.FirstOrDefaultAsync(a => a.Slug == slug);
        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        var comments = await _context.Comments
            .Include(c => c.Author)
            .Where(c => c.ArticleId == article.Id)
            .OrderBy(c => c.CreatedAt)
            .ToListAsync();

        var currentUserId = GetCurrentUserId();
        
        var followingIds = new HashSet<int>();
        if (currentUserId != null)
        {
            followingIds = (await _context.Follows
                .Where(f => f.FollowerId == currentUserId)
                .Select(f => f.FollowingId)
                .ToListAsync()).ToHashSet();
        }
        
        var commentDtos = comments.Select(c => new CommentDto(
            c.Id,
            c.CreatedAt,
            c.UpdatedAt,
            c.Body,
            new ProfileDto(c.Author.Username, c.Author.Bio, c.Author.Image, followingIds.Contains(c.AuthorId))
        )).ToArray();

        return Ok(new CommentsResponse(commentDtos));
    }

    [HttpDelete("{slug}/comments/{id}")]
    public async Task<IActionResult> DeleteComment(string slug, int id)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var article = await _context.Articles.FirstOrDefaultAsync(a => a.Slug == slug);
        if (article == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "article not found" } }));

        var comment = await _context.Comments.FirstOrDefaultAsync(c => c.Id == id && c.ArticleId == article.Id);
        if (comment == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "comment not found" } }));

        if (comment.AuthorId != currentUserId)
            return StatusCode(403, new ErrorResponse(new() { ["body"] = new[] { "forbidden" } }));

        _context.Comments.Remove(comment);
        await _context.SaveChangesAsync();

        return Ok();
    }

    private async Task<ArticleDto> MapToArticleDto(Article article, int? currentUserId)
    {
        var following = false;
        if (currentUserId != null)
        {
            following = await _context.Follows
                .AnyAsync(f => f.FollowerId == currentUserId && f.FollowingId == article.AuthorId);
        }

        var favorited = false;
        if (currentUserId != null)
        {
            favorited = article.Favorites.Any(f => f.UserId == currentUserId);
        }

        var author = new ProfileDto(article.Author.Username, article.Author.Bio, article.Author.Image, following);
        var tagList = article.Tags.Select(t => t.Name).OrderBy(t => t).ToArray();

        return new ArticleDto(
            article.Slug,
            article.Title,
            article.Description,
            article.Body,
            tagList,
            article.CreatedAt,
            article.UpdatedAt,
            favorited,
            article.Favorites.Count,
            author
        );
    }

    private int? GetCurrentUserId()
    {
        var authHeader = Request.Headers["Authorization"].FirstOrDefault();
        if (string.IsNullOrEmpty(authHeader)) return null;
        
        var token = authHeader.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)
            ? authHeader.Substring(7)
            : authHeader;
        
        return _jwtService.ValidateToken(token);
    }
}
