using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using RealWorld.Data;
using RealWorld.Services;

var builder = WebApplication.CreateBuilder(args);

builder.WebHost.ConfigureKestrel(options =>
{
    options.ListenAnyIP(3000);
});

var connectionString = Environment.GetEnvironmentVariable("POSTGRES_URI");
if (string.IsNullOrWhiteSpace(connectionString))
    connectionString = "Host=localhost;Port=5432;Database=postgres;Username=postgres;Password=password";
else if (connectionString.StartsWith("postgres://"))
{
    var uri = new Uri(connectionString);
    var userInfo = uri.UserInfo.Split(':');
    connectionString = $"Host={uri.Host};Port={uri.Port};Database={uri.AbsolutePath.TrimStart('/')};Username={userInfo[0]};Password={userInfo[1]}";
}

builder.Services.AddDbContext<ApplicationDbContext>(options =>
    options.UseNpgsql(connectionString));

builder.Services.AddScoped<IJwtService, JwtService>();
builder.Services.AddScoped<ISlugService, SlugService>();

builder.Services.AddControllers()
    .ConfigureApiBehaviorOptions(options =>
    {
        options.InvalidModelStateResponseFactory = context =>
        {
            var errors = new Dictionary<string, string[]>();
            foreach (var (key, value) in context.ModelState)
            {
                if (value.Errors.Count > 0)
                {
                    errors["body"] = value.Errors.Select(e => e.ErrorMessage).ToArray();
                    break;
                }
            }
            return new UnprocessableEntityObjectResult(new { errors });
        };
    });

var app = builder.Build();

using (var scope = app.Services.CreateScope())
{
    var db = scope.ServiceProvider.GetRequiredService<ApplicationDbContext>();
    await db.Database.MigrateAsync();
}

app.MapGet("/", () => Results.Ok());

app.MapControllers();

app.Run();
