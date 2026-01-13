package io.realworld.dto;

import java.time.LocalDateTime;

public class CommentDTO {
    private Long id;
    private LocalDateTime createdAt;
    private LocalDateTime updatedAt;
    private String body;
    private ProfileDTO author;

    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }
    
    public LocalDateTime getCreatedAt() { return createdAt; }
    public void setCreatedAt(LocalDateTime createdAt) { this.createdAt = createdAt; }
    
    public LocalDateTime getUpdatedAt() { return updatedAt; }
    public void setUpdatedAt(LocalDateTime updatedAt) { this.updatedAt = updatedAt; }
    
    public String getBody() { return body; }
    public void setBody(String body) { this.body = body; }
    
    public ProfileDTO getAuthor() { return author; }
    public void setAuthor(ProfileDTO author) { this.author = author; }
}
