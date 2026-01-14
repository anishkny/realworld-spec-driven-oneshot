using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using RealWorld.Data;
using RealWorld.DTOs;
using RealWorld.Models;
using RealWorld.Services;

namespace RealWorld.Controllers;

[ApiController]
[Route("api/profiles")]
public class ProfilesController : ControllerBase
{
    private readonly ApplicationDbContext _context;
    private readonly IJwtService _jwtService;

    public ProfilesController(ApplicationDbContext context, IJwtService jwtService)
    {
        _context = context;
        _jwtService = jwtService;
    }

    [HttpGet("{username}")]
    public async Task<IActionResult> GetProfile(string username)
    {
        var user = await _context.Users.FirstOrDefaultAsync(u => u.Username == username);
        if (user == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "profile not found" } }));

        var currentUserId = GetCurrentUserId();
        var following = false;

        if (currentUserId != null)
        {
            following = await _context.Follows
                .AnyAsync(f => f.FollowerId == currentUserId && f.FollowingId == user.Id);
        }

        var profile = new ProfileDto(user.Username, user.Bio, user.Image, following);
        return Ok(new ProfileResponse(profile));
    }

    [HttpPost("{username}/follow")]
    public async Task<IActionResult> FollowUser(string username)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var userToFollow = await _context.Users.FirstOrDefaultAsync(u => u.Username == username);
        if (userToFollow == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "profile not found" } }));

        var existingFollow = await _context.Follows
            .FirstOrDefaultAsync(f => f.FollowerId == currentUserId && f.FollowingId == userToFollow.Id);

        if (existingFollow == null)
        {
            _context.Follows.Add(new Follow
            {
                FollowerId = currentUserId.Value,
                FollowingId = userToFollow.Id
            });
            await _context.SaveChangesAsync();
        }

        var profile = new ProfileDto(userToFollow.Username, userToFollow.Bio, userToFollow.Image, true);
        return Ok(new ProfileResponse(profile));
    }

    [HttpDelete("{username}/follow")]
    public async Task<IActionResult> UnfollowUser(string username)
    {
        var currentUserId = GetCurrentUserId();
        if (currentUserId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var userToUnfollow = await _context.Users.FirstOrDefaultAsync(u => u.Username == username);
        if (userToUnfollow == null)
            return NotFound(new ErrorResponse(new() { ["body"] = new[] { "profile not found" } }));

        var follow = await _context.Follows
            .FirstOrDefaultAsync(f => f.FollowerId == currentUserId && f.FollowingId == userToUnfollow.Id);

        if (follow != null)
        {
            _context.Follows.Remove(follow);
            await _context.SaveChangesAsync();
        }

        var profile = new ProfileDto(userToUnfollow.Username, userToUnfollow.Bio, userToUnfollow.Image, false);
        return Ok(new ProfileResponse(profile));
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
