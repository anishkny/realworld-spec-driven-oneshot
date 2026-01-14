using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using RealWorld.Data;
using RealWorld.DTOs;

namespace RealWorld.Controllers;

[ApiController]
[Route("api/tags")]
public class TagsController : ControllerBase
{
    private readonly ApplicationDbContext _context;

    public TagsController(ApplicationDbContext context)
    {
        _context = context;
    }

    [HttpGet]
    public async Task<IActionResult> GetTags()
    {
        var tags = await _context.Tags
            .Select(t => t.Name)
            .OrderBy(t => t)
            .ToArrayAsync();

        return Ok(new TagsResponse(tags));
    }
}
