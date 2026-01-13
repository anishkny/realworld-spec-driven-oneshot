package io.realworld.service;

import io.realworld.dto.*;
import io.realworld.entity.User;
import io.realworld.repository.UserRepository;
import io.realworld.security.JwtUtil;
import org.mindrot.jbcrypt.BCrypt;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.util.Map;
import java.util.HashMap;

@Service
public class UserService {
    
    private final UserRepository userRepository;
    private final JwtUtil jwtUtil;
    
    public UserService(UserRepository userRepository, JwtUtil jwtUtil) {
        this.userRepository = userRepository;
        this.jwtUtil = jwtUtil;
    }
    
    @Transactional
    public Map<String, Object> registerUser(Map<String, Object> request) {
        Map<String, Object> userData = (Map<String, Object>) request.get("user");
        
        String email = (String) userData.get("email");
        String username = (String) userData.get("username");
        String password = (String) userData.get("password");
        
        Map<String, Object> errors = new HashMap<>();
        if (email == null || email.isEmpty()) {
            errors.put("email", java.util.Arrays.asList("can't be empty"));
        }
        if (username == null || username.isEmpty()) {
            errors.put("username", java.util.Arrays.asList("can't be empty"));
        }
        if (password == null || password.isEmpty()) {
            errors.put("password", java.util.Arrays.asList("can't be empty"));
        }
        
        if (!errors.isEmpty()) {
            Map<String, Object> response = new HashMap<>();
            response.put("errors", Map.of("body", errors.values().stream().flatMap(l -> ((java.util.List<String>)l).stream()).toList()));
            return response;
        }
        
        if (userRepository.existsByEmail(email)) {
            Map<String, Object> response = new HashMap<>();
            response.put("errors", Map.of("body", java.util.Arrays.asList("email already taken")));
            return response;
        }
        
        if (userRepository.existsByUsername(username)) {
            Map<String, Object> response = new HashMap<>();
            response.put("errors", Map.of("body", java.util.Arrays.asList("username already taken")));
            return response;
        }
        
        User user = new User();
        user.setEmail(email);
        user.setUsername(username);
        user.setPassword(BCrypt.hashpw(password, BCrypt.gensalt()));
        
        User savedUser = userRepository.save(user);
        
        return Map.of("user", convertToDTO(savedUser));
    }
    
    @Transactional(readOnly = true)
    public Map<String, Object> loginUser(Map<String, Object> request) {
        Map<String, Object> userData = (Map<String, Object>) request.get("user");
        
        String email = (String) userData.get("email");
        String password = (String) userData.get("password");
        
        if (email == null || email.isEmpty()) {
            return Map.of("errors", Map.of("body", java.util.Arrays.asList("email can't be empty")));
        }
        if (password == null || password.isEmpty()) {
            return Map.of("errors", Map.of("body", java.util.Arrays.asList("password can't be empty")));
        }
        
        User user = userRepository.findByEmail(email).orElse(null);
        if (user == null || !BCrypt.checkpw(password, user.getPassword())) {
            return Map.of("errors", Map.of("body", java.util.Arrays.asList("invalid credentials")));
        }
        
        return Map.of("user", convertToDTO(user));
    }
    
    @Transactional
    public Map<String, Object> updateUser(User currentUser, Map<String, Object> request) {
        Map<String, Object> userData = (Map<String, Object>) request.get("user");
        
        // Reload user to attach to current session
        User managedUser = userRepository.findById(currentUser.getId()).orElseThrow();
        
        if (userData.containsKey("email")) {
            String newEmail = (String) userData.get("email");
            if (newEmail != null && !newEmail.isEmpty() && !newEmail.matches("^[^@]+@[^@]+\\.[^@]+$")) {
                return Map.of("errors", Map.of("body", java.util.Arrays.asList("invalid email format")));
            }
            if (newEmail != null && !newEmail.equals(managedUser.getEmail())) {
                managedUser.setEmail(newEmail);
            }
        }
        if (userData.containsKey("username")) {
            managedUser.setUsername((String) userData.get("username"));
        }
        if (userData.containsKey("password")) {
            String newPassword = (String) userData.get("password");
            if (newPassword != null && !newPassword.isEmpty()) {
                managedUser.setPassword(BCrypt.hashpw(newPassword, BCrypt.gensalt()));
            }
        }
        if (userData.containsKey("bio")) {
            managedUser.setBio((String) userData.get("bio"));
        }
        if (userData.containsKey("image")) {
            managedUser.setImage((String) userData.get("image"));
        }
        
        userRepository.save(managedUser);
        
        return Map.of("user", convertToDTO(managedUser));
    }
    
    public UserDTO convertToDTO(User user) {
        UserDTO dto = new UserDTO();
        dto.setEmail(user.getEmail());
        dto.setUsername(user.getUsername());
        dto.setBio(user.getBio());
        dto.setImage(user.getImage());
        dto.setToken(jwtUtil.generateToken(user.getEmail(), user.getId()));
        return dto;
    }
    
    public ProfileDTO convertToProfileDTO(User user, User currentUser) {
        ProfileDTO dto = new ProfileDTO();
        dto.setUsername(user.getUsername());
        dto.setBio(user.getBio());
        dto.setImage(user.getImage());
        
        boolean following = false;
        if (currentUser != null) {
            following = userRepository.isFollowedBy(user.getId(), currentUser.getId());
        }
        dto.setFollowing(following);
        return dto;
    }
}
