package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;
import java.util.List;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RoomJoinedEvent extends ServerEvent {
    private String roomId;
    private String sessionId;
    private List<String> members;
    private AssignedUserDto assignedUser;

    public RoomJoinedEvent() {
        super("room.joined");
    }

    public RoomJoinedEvent(String roomId, String sessionId, List<String> members) {
        super("room.joined");
        this.roomId = roomId;
        this.sessionId = sessionId;
        this.members = members;
    }

    public RoomJoinedEvent(String roomId, String sessionId, List<String> members, AssignedUserDto assignedUser) {
        super("room.joined");
        this.roomId = roomId;
        this.sessionId = sessionId;
        this.members = members;
        this.assignedUser = assignedUser;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getSessionId() {
        return sessionId;
    }

    public void setSessionId(String sessionId) {
        this.sessionId = sessionId;
    }

    public List<String> getMembers() {
        return members;
    }

    public void setMembers(List<String> members) {
        this.members = members;
    }

    public AssignedUserDto getAssignedUser() {
        return assignedUser;
    }

    public void setAssignedUser(AssignedUserDto assignedUser) {
        this.assignedUser = assignedUser;
    }
}
