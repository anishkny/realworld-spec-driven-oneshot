package io.realworld.controller;

import io.realworld.entity.User;
import io.realworld.service.UserService;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.*;
import java.util.Map;

@RestController
@RequestMapping("/api")
public class UserController {
    
    private final UserService userService;
    
    public UserController(UserService userService) {
        this.userService = userService;
    }
    
    @PostMapping("/users")
    public ResponseEntity<Map<String, Object>> register(@RequestBody Map<String, Object> request) {
        Map<String, Object> response = userService.registerUser(request);
        if (response.containsKey("errors")) {
            return ResponseEntity.status(422).body(response);
        }
        return ResponseEntity.ok(response);
    }
    
    @PostMapping("/users/login")
    public ResponseEntity<Map<String, Object>> login(@RequestBody Map<String, Object> request) {
        Map<String, Object> response = userService.loginUser(request);
        if (response.containsKey("errors")) {
            Object errors = response.get("errors");
            Map<String, Object> body = (Map<String, Object>) errors;
            if (body.get("body").toString().contains("invalid credentials")) {
                return ResponseEntity.status(401).body(response);
            }
            return ResponseEntity.status(422).body(response);
        }
        return ResponseEntity.ok(response);
    }
    
    @GetMapping("/user")
    public ResponseEntity<Map<String, Object>> getCurrentUser(Authentication authentication) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).body(Map.of("errors", Map.of("body", java.util.Arrays.asList("unauthorized"))));
        }
        
        User currentUser = (User) authentication.getPrincipal();
        return ResponseEntity.ok(Map.of("user", userService.convertToDTO(currentUser)));
    }
    
    @PutMapping("/user")
    public ResponseEntity<Map<String, Object>> updateUser(Authentication authentication, @RequestBody Map<String, Object> request) {
        if (authentication == null || !(authentication.getPrincipal() instanceof User)) {
            return ResponseEntity.status(401).body(Map.of("errors", Map.of("body", java.util.Arrays.asList("unauthorized"))));
        }
        
        User currentUser = (User) authentication.getPrincipal();
        Map<String, Object> response = userService.updateUser(currentUser, request);
        
        if (response.containsKey("errors")) {
            return ResponseEntity.status(422).body(response);
        }
        
        return ResponseEntity.ok(response);
    }
}
