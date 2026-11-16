package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class CollabAwarenessEvent extends ServerEvent {
    private String roomId;
    private String senderSessionId;
    private String awareness;
    private AssignedUserDto assignedUser;

    public CollabAwarenessEvent() {
        super("collab.awareness");
    }

    public CollabAwarenessEvent(String roomId, String senderSessionId, String awareness) {
        this(roomId, senderSessionId, awareness, null);
    }

    public CollabAwarenessEvent(String roomId, String senderSessionId, String awareness, AssignedUserDto assignedUser) {
        super("collab.awareness");
        this.roomId = roomId;
        this.senderSessionId = senderSessionId;
        this.awareness = awareness;
        this.assignedUser = assignedUser;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getSenderSessionId() {
        return senderSessionId;
    }

    public void setSenderSessionId(String senderSessionId) {
        this.senderSessionId = senderSessionId;
    }

    public String getAwareness() {
        return awareness;
    }

    public void setAwareness(String awareness) {
        this.awareness = awareness;
    }

    public AssignedUserDto getAssignedUser() {
        return assignedUser;
    }

    public void setAssignedUser(AssignedUserDto assignedUser) {
        this.assignedUser = assignedUser;
    }
}
