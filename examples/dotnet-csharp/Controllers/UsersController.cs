using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using RealWorld.Data;
using RealWorld.DTOs;
using RealWorld.Models;
using RealWorld.Services;
using BCrypt.Net;
using System.ComponentModel.DataAnnotations;

namespace RealWorld.Controllers;

[ApiController]
[Route("api")]
public class UsersController : ControllerBase
{
    private readonly ApplicationDbContext _context;
    private readonly IJwtService _jwtService;

    public UsersController(ApplicationDbContext context, IJwtService jwtService)
    {
        _context = context;
        _jwtService = jwtService;
    }

    [HttpPost("users")]
    public async Task<IActionResult> Register([FromBody] UserRegisterRequestWrapper request)
    {
        var user = request.User;
        
        if (string.IsNullOrWhiteSpace(user.Email))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "email can't be empty" } }));
        
        if (string.IsNullOrWhiteSpace(user.Username))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "username can't be empty" } }));
        
        if (string.IsNullOrWhiteSpace(user.Password))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "password can't be empty" } }));

        if (!new EmailAddressAttribute().IsValid(user.Email))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "email is invalid" } }));

        if (await _context.Users.AnyAsync(u => u.Email == user.Email))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "email is already taken" } }));

        if (await _context.Users.AnyAsync(u => u.Username == user.Username))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "username is already taken" } }));

        var newUser = new User
        {
            Username = user.Username,
            Email = user.Email,
            PasswordHash = BCrypt.Net.BCrypt.HashPassword(user.Password)
        };

        _context.Users.Add(newUser);
        await _context.SaveChangesAsync();

        var token = _jwtService.GenerateToken(newUser.Id, newUser.Email);
        var userDto = new UserDto(newUser.Email, token, newUser.Username, newUser.Bio, newUser.Image);
        
        return Ok(new UserResponse(userDto));
    }

    [HttpPost("users/login")]
    public async Task<IActionResult> Login([FromBody] UserLoginRequestWrapper request)
    {
        var loginRequest = request.User;
        
        if (string.IsNullOrWhiteSpace(loginRequest.Email))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "email can't be empty" } }));
        
        if (string.IsNullOrWhiteSpace(loginRequest.Password))
            return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "password can't be empty" } }));

        var user = await _context.Users.FirstOrDefaultAsync(u => u.Email == loginRequest.Email);
        
        if (user == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "email or password is invalid" } }));

        if (!BCrypt.Net.BCrypt.Verify(loginRequest.Password, user.PasswordHash))
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "email or password is invalid" } }));

        var token = _jwtService.GenerateToken(user.Id, user.Email);
        var userDto = new UserDto(user.Email, token, user.Username, user.Bio, user.Image);
        
        return Ok(new UserResponse(userDto));
    }

    [HttpGet("user")]
    public async Task<IActionResult> GetCurrentUser()
    {
        var userId = GetCurrentUserId();
        if (userId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var user = await _context.Users.FindAsync(userId);
        if (user == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var token = GetAuthToken();
        var userDto = new UserDto(user.Email, token ?? "", user.Username, user.Bio, user.Image);
        
        return Ok(new UserResponse(userDto));
    }

    [HttpPut("user")]
    public async Task<IActionResult> UpdateUser([FromBody] UserUpdateRequestWrapper request)
    {
        var userId = GetCurrentUserId();
        if (userId == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var user = await _context.Users.FindAsync(userId);
        if (user == null)
            return Unauthorized(new ErrorResponse(new() { ["body"] = new[] { "unauthorized" } }));

        var update = request.User;

        if (update.Email != null)
        {
            if (!new EmailAddressAttribute().IsValid(update.Email))
                return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "email is invalid" } }));
            
            if (await _context.Users.AnyAsync(u => u.Email == update.Email && u.Id != userId))
                return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "email is already taken" } }));
            
            user.Email = update.Email;
        }

        if (update.Username != null)
        {
            if (await _context.Users.AnyAsync(u => u.Username == update.Username && u.Id != userId))
                return UnprocessableEntity(new ErrorResponse(new() { ["body"] = new[] { "username is already taken" } }));
            
            user.Username = update.Username;
        }

        if (update.Password != null)
            user.PasswordHash = BCrypt.Net.BCrypt.HashPassword(update.Password);

        if (update.Bio != null)
            user.Bio = update.Bio;

        if (update.Image != null)
            user.Image = update.Image;

        await _context.SaveChangesAsync();

        var token = GetAuthToken() ?? _jwtService.GenerateToken(user.Id, user.Email);
        var userDto = new UserDto(user.Email, token, user.Username, user.Bio, user.Image);
        
        return Ok(new UserResponse(userDto));
    }

    private int? GetCurrentUserId()
    {
        var token = GetAuthToken();
        if (token == null) return null;
        return _jwtService.ValidateToken(token);
    }

    private string? GetAuthToken()
    {
        var authHeader = Request.Headers["Authorization"].FirstOrDefault();
        if (string.IsNullOrEmpty(authHeader))
            return null;
        
        return authHeader.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)
            ? authHeader.Substring(7)
            : authHeader;
    }
}
