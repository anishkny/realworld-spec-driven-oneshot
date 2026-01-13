package io.realworld.dto;

import java.util.List;

public class UserDTO {
    private String email;
    private String token;
    private String username;
    private String bio;
    private String image;

    public String getEmail() { return email; }
    public void setEmail(String email) { this.email = email; }
    
    public String getToken() { return token; }
    public void setToken(String token) { this.token = token; }
    
    public String getUsername() { return username; }
    public void setUsername(String username) { this.username = username; }
    
    public String getBio() { return bio; }
    public void setBio(String bio) { this.bio = bio; }
    
    public String getImage() { return image; }
    public void setImage(String image) { this.image = image; }
}
