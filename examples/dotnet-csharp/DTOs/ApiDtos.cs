namespace RealWorld.DTOs;

public record UserDto(string Email, string Token, string Username, string? Bio, string? Image);
public record UserResponse(UserDto User);

public record UserRegisterRequest(string Username, string Email, string Password);
public record UserLoginRequest(string Email, string Password);
public record UserRegisterRequestWrapper(UserRegisterRequest User);
public record UserLoginRequestWrapper(UserLoginRequest User);

public record UserUpdateRequest(string? Email, string? Username, string? Password, string? Bio, string? Image);
public record UserUpdateRequestWrapper(UserUpdateRequest User);

public record ProfileDto(string Username, string? Bio, string? Image, bool Following);
public record ProfileResponse(ProfileDto Profile);

public record ArticleDto(
    string Slug,
    string Title,
    string Description,
    string Body,
    string[] TagList,
    DateTime CreatedAt,
    DateTime UpdatedAt,
    bool Favorited,
    int FavoritesCount,
    ProfileDto Author
);
public record ArticleResponse(ArticleDto Article);
public record ArticlesResponse(ArticleDto[] Articles, int ArticlesCount);

public record ArticleCreateRequest(string Title, string Description, string Body, string[]? TagList);
public record ArticleCreateRequestWrapper(ArticleCreateRequest Article);

public record ArticleUpdateRequest(string? Title, string? Description, string? Body);
public record ArticleUpdateRequestWrapper(ArticleUpdateRequest Article);

public record CommentDto(int Id, DateTime CreatedAt, DateTime UpdatedAt, string Body, ProfileDto Author);
public record CommentResponse(CommentDto Comment);
public record CommentsResponse(CommentDto[] Comments);

public record CommentCreateRequest(string Body);
public record CommentCreateRequestWrapper(CommentCreateRequest Comment);

public record TagsResponse(string[] Tags);

public record ErrorResponse(Dictionary<string, string[]> Errors);
