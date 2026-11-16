package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;
import java.util.List;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RoomPeerJoinedEvent extends ServerEvent {
    private String roomId;
    private String sessionId;
    private List<String> members;
    private AssignedUserDto assignedUser;

    public RoomPeerJoinedEvent() {
        super("room.peer_joined");
    }

    public RoomPeerJoinedEvent(String roomId, String sessionId, List<String> members) {
        super("room.peer_joined");
        this.roomId = roomId;
        this.sessionId = sessionId;
        this.members = members;
    }

    public RoomPeerJoinedEvent(String roomId, String sessionId, List<String> members, AssignedUserDto assignedUser) {
        super("room.peer_joined");
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
